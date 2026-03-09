
#include <QApplication>
#include <QCoreApplication>
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GLES3/gl3.h>
#endif
#include "camera_view.h"

void VipcWorker::stop() {
  running_ = false;
  QThread *thread = QThread::currentThread();
  QThread *mainThread = QCoreApplication::instance() ? QCoreApplication::instance()->thread() : nullptr;
  if (thread && thread != mainThread) {
    thread->requestInterruption();
  }
}

namespace {

const char frame_vertex_shader[] =
#ifdef __APPLE__
    "#version 330 core\n"
#else
    "#version 300 es\n"
#endif
    "layout(location = 0) in vec2 aPosition;\n"
    "layout(location = 1) in vec2 aTexCoord;\n"
    "uniform mat4 uTransform;\n"
    "out vec2 vTexCoord;\n"
    "void main() {\n"
    "  gl_Position = uTransform * vec4(aPosition, 0.0, 1.0);\n"
    "  vTexCoord = aTexCoord;\n"
    "}\n";

const char frame_fragment_shader[] =
#ifdef __APPLE__
    "#version 330 core\n"
#else
    "#version 300 es\n"
    "precision mediump float;\n"
#endif
    "uniform sampler2D uTextureY;\n"
    "uniform sampler2D uTextureUV;\n"
    "in vec2 vTexCoord;\n"
    "out vec4 colorOut;\n"
    "void main() {\n"
    "  float y = texture(uTextureY, vTexCoord).r;\n"
    "  vec2 uv = texture(uTextureUV, vTexCoord).rg - 0.5;\n"
    "  float r = y + 1.402 * uv.y;\n"
    "  float g = y - 0.344 * uv.x - 0.714 * uv.y;\n"
    "  float b = y + 1.772 * uv.x;\n"
    "  colorOut = vec4(r, g, b, 1.0);\n"
    "}\n";

}  // namespace

CameraView::CameraView(std::string stream_name, VisionStreamType type, QWidget* parent)
    : QOpenGLWidget(parent),
      stream_name(std::move(stream_name)),
      active_stream_type(type),
      requested_stream_type(type) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  qRegisterMetaType<std::set<VisionStreamType>>("availableStreams");
  connect(QApplication::instance(), &QCoreApplication::aboutToQuit, this, &CameraView::stopVipcThread);
}

CameraView::~CameraView() {
  stopVipcThread();
  makeCurrent();
  if (isValid()) {
    glDeleteVertexArrays(1, &frame_vao);
    glDeleteBuffers(1, &frame_vbo);
    glDeleteBuffers(1, &frame_ibo);
    glDeleteTextures(2, textures);
    shader_program_.reset();
  }
  doneCurrent();
}

void CameraView::initializeGL() {
  initializeOpenGLFunctions();

  shader_program_ = std::make_unique<QOpenGLShaderProgram>(context());
  bool ok = shader_program_->addShaderFromSourceCode(QOpenGLShader::Vertex, frame_vertex_shader);
  assert(ok);
  ok = shader_program_->addShaderFromSourceCode(QOpenGLShader::Fragment, frame_fragment_shader);
  assert(ok);
  ok = shader_program_->link();
  assert(ok);

  transform_loc_ = shader_program_->uniformLocation("uTransform");
  assert(transform_loc_ != -1);

  GLint frame_pos_loc = shader_program_->attributeLocation("aPosition");
  GLint frame_texcoord_loc = shader_program_->attributeLocation("aTexCoord");

  auto [x1, x2, y1, y2] =
      requested_stream_type == VISION_STREAM_DRIVER ? std::tuple(0.f, 1.f, 1.f, 0.f) : std::tuple(1.f, 0.f, 1.f, 0.f);
  const uint8_t frame_indicies[] = {0, 1, 2, 0, 2, 3};
  const float frame_coords[4][4] = {
      {-1.0, -1.0, x2, y1},  // bl
      {-1.0, 1.0, x2, y2},   // tl
      {1.0, 1.0, x1, y2},    // tr
      {1.0, -1.0, x1, y1},   // br
  };

  glGenVertexArrays(1, &frame_vao);
  glBindVertexArray(frame_vao);
  glGenBuffers(1, &frame_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, frame_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(frame_coords), frame_coords, GL_STATIC_DRAW);
  glEnableVertexAttribArray(frame_pos_loc);
  glVertexAttribPointer(frame_pos_loc, 2, GL_FLOAT, GL_FALSE, sizeof(frame_coords[0]), (const void*)0);
  glEnableVertexAttribArray(frame_texcoord_loc);
  glVertexAttribPointer(frame_texcoord_loc, 2, GL_FLOAT, GL_FALSE, sizeof(frame_coords[0]),
                        (const void*)(sizeof(float) * 2));
  glGenBuffers(1, &frame_ibo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, frame_ibo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(frame_indicies), frame_indicies, GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);

  glGenTextures(2, textures);

  shader_program_->bind();
  shader_program_->setUniformValue("uTextureY", 0);
  shader_program_->setUniformValue("uTextureUV", 1);
  shader_program_->release();
}

void CameraView::showEvent(QShowEvent* event) { startVipcThread(); }

void CameraView::hideEvent(QHideEvent* event) { stopVipcThread(); }

void CameraView::startVipcThread() {
  if (vipc_thread_) return;

  worker_ = new VipcWorker(this);
  vipc_thread_ = new QThread(this);
  worker_->moveToThread(vipc_thread_);

  connect(vipc_thread_, &QThread::started, worker_, &VipcWorker::run);
  connect(worker_, &VipcWorker::connected, this, &CameraView::onVipcConnected);
  connect(worker_, &VipcWorker::frameReceived, this, &CameraView::onFrameReceived);
  connect(worker_, &VipcWorker::availableStreamsUpdated, this, &CameraView::onAvailableStreamsUpdated);
  connect(vipc_thread_, &QThread::finished, worker_, &QObject::deleteLater);

  vipc_thread_->start();
}

void CameraView::stopVipcThread() {
  if (!vipc_thread_) return;

  // Signal the worker to stop and interrupt the thread
  worker_->stop();
  vipc_thread_->requestInterruption();
  vipc_thread_->quit(); // in case an event loop is running

  // Wait for the thread to finish, but with a timeout to avoid deadlock
  if (!vipc_thread_->wait(500)) {
    qWarning("CameraView: vipc_thread_ did not exit in time, forcing termination");
    // As a last resort, terminate (not recommended, but avoids app hang)
    vipc_thread_->terminate();
    vipc_thread_->wait(100);
  }
  vipc_thread_->deleteLater();
  vipc_thread_ = nullptr;
  worker_ = nullptr;

  std::lock_guard lk(frame_lock);
  current_frame_ = nullptr;
  available_streams.clear();
}

void CameraView::onAvailableStreamsUpdated(std::set<VisionStreamType> streams) {
  available_streams = streams;
  emit vipcAvailableStreamsUpdated(streams);
}

void CameraView::paintGL() {
  glClearColor(bg.redF(), bg.greenF(), bg.blueF(), bg.alphaF());
  glClear(GL_STENCIL_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

  std::lock_guard lk(frame_lock);
  if (!current_frame_ || stream_width <= 0 || stream_height <= 0 || height() <= 0) return;

  float widget_ratio = (float)width() / height();
  float frame_ratio = (float)stream_width / stream_height;
  float scale_x = std::min(frame_ratio / widget_ratio, 1.0f);
  float scale_y = std::min(widget_ratio / frame_ratio, 1.0f);

  glViewport(0, 0, width() * devicePixelRatio(), height() * devicePixelRatio());

  shader_program_->bind();
  QMatrix4x4 transform;
  transform.scale(scale_x, scale_y, 1.0f);
  shader_program_->setUniformValue(transform_loc_, transform);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, textures[0]);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, stream_stride);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, stream_width, stream_height, GL_RED, GL_UNSIGNED_BYTE, current_frame_->y);

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, textures[1]);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, stream_stride / 2);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, stream_width / 2, stream_height / 2, GL_RG, GL_UNSIGNED_BYTE,
                  current_frame_->uv);

  glBindVertexArray(frame_vao);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, nullptr);
  glBindVertexArray(0);

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

  shader_program_->release();
}

void CameraView::initTexture(GLuint texture, GLint internal_format, int width, int height, GLenum format) {
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, GL_UNSIGNED_BYTE, nullptr);
  assert(glGetError() == GL_NO_ERROR);
}

void CameraView::onVipcConnected(int width, int height, int stride) {
  makeCurrent();
  stream_width = width;
  stream_height = height;
  stream_stride = stride;

  initTexture(textures[0], GL_R8, stream_width, stream_height, GL_RED);
  initTexture(textures[1], GL_RG8, stream_width / 2, stream_height / 2, GL_RG);
  glBindTexture(GL_TEXTURE_2D, 0);
  doneCurrent();
}

void CameraView::onFrameReceived() { update(); }

// --- VipcWorker ---

VipcWorker::VipcWorker(CameraView* view) : view_(view) {}

void VipcWorker::clearFrame() {
  std::lock_guard lk(view_->frame_lock);
  view_->current_frame_ = nullptr;
}

void VipcWorker::run() {
  VisionStreamType cur_stream = view_->requested_stream_type;
  std::unique_ptr<VisionIpcClient> vipc_client;

  while (running_ && !QThread::currentThread()->isInterruptionRequested()) {
    if (!vipc_client || cur_stream != view_->requested_stream_type) {
      clearFrame();
      cur_stream = view_->requested_stream_type;
      vipc_client = std::make_unique<VisionIpcClient>(view_->stream_name, cur_stream, false);
    }
    view_->active_stream_type = cur_stream;

    if (!vipc_client->connected) {
      clearFrame();
      auto streams = VisionIpcClient::getAvailableStreams(view_->stream_name, false);
      if (streams.empty()) {
        for (int i = 0; i < 10; ++i) {
          if (!running_ || QThread::currentThread()->isInterruptionRequested()) return;
          QThread::msleep(10);
        }
        continue;
      }
      emit availableStreamsUpdated(streams);

      if (!vipc_client->connect(false)) {
        for (int i = 0; i < 10; ++i) {
          if (!running_ || QThread::currentThread()->isInterruptionRequested()) return;
          QThread::msleep(10);
        }
        continue;
      }
      emit connected(vipc_client->buffers[0].width, vipc_client->buffers[0].height, vipc_client->buffers[0].stride);
    }

    VisionIpcBufExtra meta = {};
    if (VisionBuf* buf = vipc_client->recv(&meta, 20)) { // shorter timeout for responsiveness
      {
        std::lock_guard lk(view_->frame_lock);
        view_->current_frame_ = buf;
        view_->frame_meta_ = meta;
      }
      emit frameReceived();
    }
    if (!running_ || QThread::currentThread()->isInterruptionRequested()) break;
  }

  // Ensure frame pointer is cleared before client is destroyed
  clearFrame();
}
