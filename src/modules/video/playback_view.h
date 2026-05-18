#pragma once

#include <QPixmap>
#include <QTimer>
#include <memory>

#include "camera_view.h"
#include "replay/include/logreader.h"
#include "replay/include/timeline.h"
#include "thumbnail_cache.h"

const int THUMBNAIL_MARGIN = 3;
const int MIN_VIDEO_HEIGHT = 100;

static const QColor timeline_colors[] = {
  [(int)TimelineType::None] = QColor(111, 143, 175),
  [(int)TimelineType::Engaged] = QColor(0, 163, 108),
  [(int)TimelineType::UserBookmark] = Qt::magenta,
  [(int)TimelineType::AlertInfo] = Qt::green,
  [(int)TimelineType::AlertWarning] = QColor(255, 195, 0),
  [(int)TimelineType::AlertCritical] = QColor(199, 0, 57),
};

class PlaybackCameraView : public CameraView {
  Q_OBJECT

 public:
  PlaybackCameraView(std::string stream_name, VisionStreamType stream_type, QWidget* parent = nullptr);
  void paintGL() override;

  // Shadows CameraView::setStreamType to also retarget the thumbnail cache.
  void setStreamType(VisionStreamType type);

  // Update hover time and (debounced) kick off an async thumbnail request.
  // seconds < 0 hides any thumbnail overlay.
  void setHoverTime(double seconds);

 private slots:
  void onThumbnailReady(double seconds, QPixmap pixmap);
  void onHoverDebounceTimeout();

 private:
  QPixmap decorateScrubThumbnail(const QPixmap& thumb, double seconds);
  QPixmap decorateHoverThumbnail(const QPixmap& thumb, double seconds);
  void drawAlert(QPainter& p, const QRect& rect, const Timeline::Entry& alert);
  void drawThumbnail(QPainter& p);
  void drawScrubThumbnail(QPainter& p);
  void drawTime(QPainter& p, const QRect& rect, double seconds);

  ThumbnailCache thumb_cache_;
  QTimer hover_debounce_;
  double pending_hover_time_ = -1;
  double thumbnail_dispaly_time = -1;

  // Last decoded thumbnail (kept as the most recent full-res pixmap).
  QPixmap last_thumb_pixmap_;
  double last_thumb_time_ = -1;

  friend class VideoPlayer;
};
