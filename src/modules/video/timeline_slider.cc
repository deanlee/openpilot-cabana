#include "timeline_slider.h"

#include <QMouseEvent>
#include <QPainter>

#include "core/streams/replay_stream.h"
#include "modules/system/stream_manager.h"
#include "playback_view.h"
#include "replay/include/timeline.h"

const int kMargin = 9; // Scrubber radius

static Replay* getReplay() {
  auto stream = qobject_cast<ReplayStream*>(StreamManager::stream());
  return stream ? stream->getReplay() : nullptr;
}

TimelineSlider::TimelineSlider(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  setMouseTracking(true);
  setFixedHeight(22);
}

void TimelineSlider::setRange(double min, double max) {
  if (min_time != min || max_time != max) {
    min_time = min;
    max_time = max;
    updateCache();
  }
}

void TimelineSlider::setTime(double t) {
  if (is_scrubbing) return;
  double new_x = timeToX(t);
  double old_x = timeToX(current_time);

  if (std::abs(new_x - old_x) >= 1.0) {
    current_time = t;
    update(QRect(old_x - 12, 0, 24, height()));
    update(QRect(new_x - 12, 0, 24, height()));
  }
}

void TimelineSlider::setThumbnailTime(double t) {
  if (thumbnail_display_time != t) {
    thumbnail_display_time = t;
    emit timeHovered(t);
    update();
  }
}

void TimelineSlider::paintEvent(QPaintEvent* ev) {
  QPainter p(this);
  const int track_w = width() - kMargin * 2;
  if (max_time <= min_time || track_w <= 0) return;

  double scale = (double)track_w / (max_time - min_time);

  if (timeline_cache.width() != track_w) {
    timeline_cache = QPixmap(track_w, height());
    timeline_cache.fill(palette().window().color());
    QPainter cp(&timeline_cache);
    int gy = (height() - 6) / 2;
    cp.fillRect(0, gy, track_w, 6, timeline_colors[(int)TimelineType::None]);
    drawEvents(cp, gy, 6, scale);
    drawUnloadedOverlay(cp, gy, 6, scale);
  }

  p.fillRect(rect(), palette().window());
  p.drawPixmap(kMargin, 0, timeline_cache);
  p.setRenderHint(QPainter::Antialiasing);

  if (thumbnail_display_time >= 0) {
    p.fillRect(QRectF(timeToX(thumbnail_display_time) - 1, 0, 2, height()), palette().highlight());
  }
  drawScrubber(p, height(), scale);
}

void TimelineSlider::drawEvents(QPainter& p, int y, int h, double scale) {
  auto replay = getReplay();
  if (!replay) return;

  for (const auto& entry : *replay->getTimeline()) {
    if (entry.end_time < min_time || entry.start_time > max_time) continue;

    int x1 = std::max(0.0, (entry.start_time - min_time) * scale);
    int x2 = std::min((double)timeline_cache.width(), (entry.end_time - min_time) * scale);

    if (x2 > x1) {
      p.fillRect(x1, y, std::max(1, x2 - x1), h, timeline_colors[(int)entry.type]);
    }
  }
}

void TimelineSlider::drawUnloadedOverlay(QPainter& p, int y, int h, double scale) {
  auto replay = getReplay();
  if (!replay || !replay->getEventData()) return;

  QColor overlay = palette().color(QPalette::Window);
  overlay.setAlpha(160);

  for (const auto& [n, _] : replay->route().segments()) {
    double start = n * 60.0;
    double end = start + 60.0;

    if (end > min_time && start < max_time && !replay->getEventData()->isSegmentLoaded(n)) {
      int x1 = std::max(0.0, (start - min_time) * scale);
      int x2 = std::min((double)timeline_cache.width(), (end - min_time) * scale);
      p.fillRect(x1, y, x2 - x1, h, overlay);
    }
  }
}

void TimelineSlider::drawScrubber(QPainter& p, int h, double scale) {
  double x = timeToX(current_time);
  QColor highlight = palette().highlight().color();
  p.setPen(QPen(QColor(0, 0, 0, 80), 3));
  p.drawLine(QPointF(x, 0), QPointF(x, h));
  p.setPen(QPen(highlight, 1));
  p.drawLine(QPointF(x, 0), QPointF(x, h));

  double r = (is_hovered || is_scrubbing) ? 8 : 7.0;
  p.setPen(QPen(highlight, 1.5));
  p.setBrush(palette().button());
  p.drawEllipse(QPointF(x, h / 2.0), r, r);
  p.setBrush(palette().windowText());
  p.setPen(Qt::NoPen);
  p.drawEllipse(QPointF(x, h / 2.0), 1.5, 1.5);
}

void TimelineSlider::handleMouse(int x) {
  double seek_to = xToTime(x);
  if (std::abs(seek_to - last_sent_seek_time) > 0.1 || !is_scrubbing) {
    StreamManager::stream()->seekTo(seek_to);
    last_sent_seek_time = seek_to;
  }
  current_time = seek_to;
  update();
}

void TimelineSlider::mousePressEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton) {
    is_scrubbing = true;
    auto stream = StreamManager::stream();
    resume_after_scrub = stream && !stream->isPaused();

    if (resume_after_scrub) stream->pause(true);
    handleMouse(e->x());
  }
}

void TimelineSlider::mouseMoveEvent(QMouseEvent* e) {
  setThumbnailTime(xToTime(e->x()));
  bool near = std::abs(e->pos().x() - timeToX(current_time)) < 20;
  if (near != is_hovered) { is_hovered = near; update(); }
  if (is_scrubbing) handleMouse(e->x());
}

void TimelineSlider::mouseReleaseEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton && is_scrubbing) {
    is_scrubbing = false;
    if (resume_after_scrub) {
      StreamManager::stream()->pause(false);
      resume_after_scrub = false;
    }
    last_sent_seek_time = -1.0;
    update();
  }
}

void TimelineSlider::changeEvent(QEvent* e) {
  if (e->type() == QEvent::PaletteChange || e->type() == QEvent::StyleChange) {
    updateCache();
  }
  QWidget::changeEvent(e);
}

void TimelineSlider::leaveEvent(QEvent* e) {
  is_hovered = false;
  setThumbnailTime(-1);
  QWidget::leaveEvent(e);
}

void TimelineSlider::updateCache() {
  timeline_cache = QPixmap();
  update();
}

double TimelineSlider::timeToX(double t) const {
  const double range = max_time - min_time;
  if (range <= 0) return kMargin;
  return kMargin + (t - min_time) * (width() - kMargin * 2) / range;
}

double TimelineSlider::xToTime(int x) const {
  const int track_w = width() - kMargin * 2;
  if (track_w <= 0) return min_time;
  double t = min_time + (double)(x - kMargin) / track_w * (max_time - min_time);
  return std::clamp(t, min_time, max_time);
}
