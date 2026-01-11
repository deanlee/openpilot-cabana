#pragma once

#include <memory>

#include "camera_view.h"
#include "replay/include/logreader.h"
#include "replay/include/timeline.h"

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
  void parseQLog(std::shared_ptr<LogReader> qlog);

 private:
  QPixmap generateThumbnail(QPixmap thumbnail, double seconds);
  void drawAlert(QPainter& p, const QRect& rect, const Timeline::Entry& alert);
  void drawThumbnail(QPainter& p);
  void drawScrubThumbnail(QPainter& p);
  void drawTime(QPainter& p, const QRect& rect, double seconds);

  QMap<uint64_t, QPixmap> big_thumbnails;
  QMap<uint64_t, QPixmap> thumbnails;
  double thumbnail_dispaly_time = -1;
  friend class VideoPlayer;
};
