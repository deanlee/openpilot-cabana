#pragma once

#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QThread>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "msgq/visionipc/visionipc_client.h"

class CameraView;

class VipcWorker : public QObject {
  Q_OBJECT

 public:
  VipcWorker(CameraView* view);
  void stop();

 public slots:
  void run();

 signals:
  void connected(int width, int height, int stride);
  void frameReceived();
  void availableStreamsUpdated(std::set<VisionStreamType> streams);

 private:
  void clearFrame();

  CameraView* view_;
  std::atomic<bool> running_{true};
};

class CameraView : public QOpenGLWidget, protected QOpenGLFunctions {
  Q_OBJECT

 public:
  explicit CameraView(std::string stream_name, VisionStreamType stream_type, QWidget* parent = nullptr);
  ~CameraView();
  void setStreamType(VisionStreamType type) { requested_stream_type = type; }
  VisionStreamType getStreamType() { return active_stream_type; }

 signals:
  void clicked();
  void vipcAvailableStreamsUpdated(std::set<VisionStreamType>);

 protected:
  void paintGL() override;
  void initializeGL() override;
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override { emit clicked(); }

 private:
  friend class VipcWorker;

  void startVipcThread();
  void stopVipcThread();
  void onVipcConnected(int width, int height, int stride);
  void onFrameReceived();
  void onAvailableStreamsUpdated(std::set<VisionStreamType> streams);
  void initTexture(GLuint texture, GLint internal_format, int width, int height, GLenum format);

  GLuint frame_vao = 0, frame_vbo = 0, frame_ibo = 0;
  GLuint textures[2] = {};
  std::unique_ptr<QOpenGLShaderProgram> shader_program_;
  GLint transform_loc_ = -1;

  int stream_width = 0;
  int stream_height = 0;
  int stream_stride = 0;

  QThread* vipc_thread_ = nullptr;
  VipcWorker* worker_ = nullptr;
  std::mutex frame_lock;
  VisionIpcBufExtra frame_meta_ = {};

 protected:
  QColor bg = Qt::black;
  std::string stream_name;
  std::atomic<VisionStreamType> active_stream_type;
  std::atomic<VisionStreamType> requested_stream_type;
  std::set<VisionStreamType> available_streams;
  VisionBuf* current_frame_ = nullptr;
};

Q_DECLARE_METATYPE(std::set<VisionStreamType>);
