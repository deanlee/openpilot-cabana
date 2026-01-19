#pragma once

#include <QPainter>
#include <QPixmapCache>
#include <QWidget>

class TimelineSlider : public QWidget {
  Q_OBJECT

 public:
  explicit TimelineSlider(QWidget* parent = nullptr);

  inline double maximum() const { return max_time; }
  inline double minimum() const { return min_time; }

  void setRange(double min, double max);
  void setTime(double t);
  void setThumbnailTime(double t);
  void updateCache();

 protected:
  void changeEvent(QEvent* ev) override;
  void paintEvent(QPaintEvent* ev) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void leaveEvent(QEvent* e) override;

 signals:
  void timeHovered(double time);

 private:
  void handleMouse(int x);
  void drawEvents(QPainter &p, int y, int h, double scale);
  void drawUnloadedOverlay(QPainter &p, int y, int h, double scale);
  void drawMarkers(QPainter &p, int h, double scale);
  void drawScrubber(QPainter &p, int h, double scale);

  double min_time = 0;
  double max_time = 0;
  double current_time = 0;
  double thumbnail_display_time = -1;

  bool is_hovered = false;
  bool is_scrubbing = false;
  bool resume_after_scrub = false;
  double last_sent_seek_time = -1.0;

  QPixmap timeline_cache;
};
