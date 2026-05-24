#include "playback_view.h"

#include <cmath>

#include "modules/system/stream_manager.h"

namespace {
constexpr int kHoverDebounceMs = 80;
constexpr int kScrubThrottleMs = 40;
}  // namespace

static Replay* getReplay() {
  auto stream = qobject_cast<ReplayStream*>(StreamManager::stream());
  return stream ? stream->getReplay() : nullptr;
}

PlaybackCameraView::PlaybackCameraView(std::string stream_name, VisionStreamType stream_type, QWidget* parent)
    : CameraView(stream_name, stream_type, parent) {
  thumb_cache_.setStreamType(stream_type);
  connect(&thumb_cache_, &ThumbnailCache::thumbnailReady, this, &PlaybackCameraView::onThumbnailReady);

  hover_debounce_.setSingleShot(true);
  connect(&hover_debounce_, &QTimer::timeout, this, &PlaybackCameraView::onHoverDebounceTimeout);

  connect(&StreamManager::instance(), &StreamManager::streamChanged, &thumb_cache_, &ThumbnailCache::clear);
  connect(&StreamManager::instance(), &StreamManager::seeking, &thumb_cache_, &ThumbnailCache::clear);
}

void PlaybackCameraView::setStreamType(VisionStreamType type) {
  CameraView::setStreamType(type);
  thumb_cache_.setStreamType(type);
}

void PlaybackCameraView::setHoverTime(double seconds) {
  auto* stream = StreamManager::stream();
  if (!stream) return;

  if (seconds < 0) {
    pending_hover_time_ = -1;
    thumbnail_dispaly_time = -1;
    hover_debounce_.stop();
    update();
    return;
  }
  thumbnail_dispaly_time = seconds;
  pending_hover_time_ = seconds;

  bool scrubbing = stream && stream->isPaused();
  if (scrubbing) {
    // During active scrub drags, classic debounce can starve requests because
    // every move restarts the timer. Use leading-edge dispatch + fixed
    // throttle window so thumbnails keep updating while dragging.
    if (!hover_debounce_.isActive()) {
      onHoverDebounceTimeout();
      hover_debounce_.start(kScrubThrottleMs);
    }
  } else {
    // Hover preview mode keeps debounce behavior to reduce decode churn.
    hover_debounce_.start(kHoverDebounceMs);
  }
  update();
}

void PlaybackCameraView::onHoverDebounceTimeout() {
  if (pending_hover_time_ < 0) return;
  auto* stream = StreamManager::stream();
  if (!stream) return;

  // Request a height appropriate for the current display mode. When paused we
  // render a full-frame scrub thumbnail; otherwise a small corner thumbnail.
  bool scrubbing = stream->isPaused();
  int target_h = scrubbing ? height() : (MIN_VIDEO_HEIGHT - THUMBNAIL_MARGIN * 2);
  thumb_cache_.requestThumbnail(pending_hover_time_, std::max(target_h, 64));
}

void PlaybackCameraView::onThumbnailReady(double seconds, QPixmap pixmap) {
  // Ignore stale callbacks from older hover requests.
  if (thumbnail_dispaly_time >= 0 && std::abs(seconds - thumbnail_dispaly_time) > 0.5) return;

  last_thumb_pixmap_ = pixmap;
  last_thumb_time_ = seconds;
  update();
}

void PlaybackCameraView::paintGL() {
  if (!StreamManager::instance().isReplayStream()) {
    glClearColor(bg.redF(), bg.greenF(), bg.blueF(), bg.alphaF());
    glClear(GL_STENCIL_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    return;
  }

  CameraView::paintGL();

  auto* stream = StreamManager::stream();
  auto* replay = getReplay();
  if (!stream || !replay) return;

  QPainter p(this);
  bool scrubbing = false;
  if (thumbnail_dispaly_time >= 0) {
    scrubbing = stream->isPaused();
    scrubbing ? drawScrubThumbnail(p) : drawThumbnail(p);
  }
  if (auto alert = replay->findAlertAtTime(scrubbing ? thumbnail_dispaly_time : stream->currentSec())) {
    drawAlert(p, rect(), *alert);
  }

  if (stream->isPaused()) {
    p.setPen(QColor(200, 200, 200));
    p.setFont(QFont(font().family(), 16, QFont::Bold));
    p.drawText(rect(), Qt::AlignCenter, tr("PAUSED"));
  } else if (!current_frame_) {
    p.setRenderHint(QPainter::Antialiasing);
    QColor gray(130, 130, 130);
    int icon_size = 32;
    QPixmap icon = utils::icon("video-off", QSize(icon_size, icon_size), gray);
    int cx = rect().center().x();
    int cy = rect().center().y();
    p.drawPixmap(cx - (icon_size / 2), cy - 27, icon);
    p.setPen(gray);
    p.setFont(QFont("sans-serif", 9, QFont::DemiBold));
    p.drawText(rect().adjusted(0, 25, 0, 25), Qt::AlignCenter, tr("No Video"));
  }
}

QPixmap PlaybackCameraView::decorateHoverThumbnail(const QPixmap& thumb, double seconds) {
  QPixmap scaled = thumb.scaledToHeight(MIN_VIDEO_HEIGHT - THUMBNAIL_MARGIN * 2, Qt::SmoothTransformation);
  QPainter p(&scaled);
  p.setPen(QPen(palette().color(QPalette::BrightText), 2));
  p.drawRect(scaled.rect());
  if (auto replay = getReplay()) {
    if (auto alert = replay->findAlertAtTime(seconds)) {
      p.setFont(QFont(font().family(), 10));
      drawAlert(p, scaled.rect(), *alert);
    }
  }
  return scaled;
}

QPixmap PlaybackCameraView::decorateScrubThumbnail(const QPixmap& thumb) {
  return thumb.scaled(rect().size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void PlaybackCameraView::drawScrubThumbnail(QPainter& p) {
  p.fillRect(rect(), Qt::black);
  if (last_thumb_pixmap_.isNull()) return;
  QPixmap scaled_thumb = decorateScrubThumbnail(last_thumb_pixmap_);
  QRect thumb_rect(rect().center() - scaled_thumb.rect().center(), scaled_thumb.size());
  p.drawPixmap(thumb_rect.topLeft(), scaled_thumb);
  drawTime(p, thumb_rect, thumbnail_dispaly_time);
}

void PlaybackCameraView::drawThumbnail(QPainter& p) {
  if (last_thumb_pixmap_.isNull()) return;
  QPixmap thumb = decorateHoverThumbnail(last_thumb_pixmap_, last_thumb_time_);
  auto* stream = StreamManager::stream();
  if (!stream) return;
  auto [min_sec, max_sec] = stream->timeRange().value_or(
      std::make_pair(stream->minSeconds(), stream->maxSeconds()));
  if (max_sec <= min_sec) return;
  int pos = (thumbnail_dispaly_time - min_sec) * width() / (max_sec - min_sec);
  int x = std::clamp(pos - thumb.width() / 2, THUMBNAIL_MARGIN, width() - thumb.width() - THUMBNAIL_MARGIN + 1);
  int y = height() - thumb.height() - THUMBNAIL_MARGIN;

  p.drawPixmap(x, y, thumb);
  drawTime(p, QRect{x, y, thumb.width(), thumb.height()}, thumbnail_dispaly_time);
}

void PlaybackCameraView::drawTime(QPainter& p, const QRect& rect, double seconds) {
  p.setPen(palette().color(QPalette::BrightText));
  p.setFont(QFont(font().family(), 10));
  p.drawText(rect.adjusted(0, 0, 0, -THUMBNAIL_MARGIN), Qt::AlignHCenter | Qt::AlignBottom,
             QString::number(seconds, 'f', 3));
}

void PlaybackCameraView::drawAlert(QPainter& p, const QRect& rect, const Timeline::Entry& alert) {
  p.setPen(QPen(palette().color(QPalette::BrightText), 2));
  QColor color = timeline_colors[int(alert.type)];
  color.setAlphaF(0.5);
  QString text = QString::fromStdString(alert.text1);
  if (!alert.text2.empty()) text += "\n" + QString::fromStdString(alert.text2);

  QRect text_rect = rect.adjusted(1, 1, -1, -1);
  QRect r = p.fontMetrics().boundingRect(text_rect, Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap, text);
  p.fillRect(text_rect.left(), r.top(), text_rect.width(), r.height(), color);
  p.drawText(text_rect, Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap, text);
}
