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

  const double range = max_time - min_time;
  if (range <= 0) return;

  const int track_w = width() - (kMargin * 2);
  const double new_x = kMargin + (t - min_time) * (track_w / range);
  const double old_x = kMargin + (current_time - min_time) * (track_w / range);

  if (std::abs(new_x - old_x) >= 1.0) {
    current_time = t;
    update(QRect(old_x - 12, 0, 24, height()));
    update(QRect(new_x - 12, 0, 24, height()));
  }
}

void TimelineSlider::setThumbnailTime(double t) {
  if (thumbnail_display_time == t) return;
  thumbnail_display_time = t;
  emit timeHovered(t);
  update();
}

void TimelineSlider::paintEvent(QPaintEvent* ev) {
  QPainter p(this);
  const double range = max_time - min_time;
  if (range <= 0 || width() <= kMargin * 2) return;

  const int w = width();
  const int h = height();
  const int track_w = w - (kMargin * 2);
  const double scale = (double)track_w / range;

  // 1. Static Data Layer (Narrower Track)
  if (timeline_cache.width() != track_w) {
    timeline_cache = QPixmap(track_w, h);
    timeline_cache.fill(palette().window().color());
    QPainter cache_p(&timeline_cache);

    const int groove_h = 6;
    const int groove_y = (h - groove_h) / 2;

    // Background of the track
    cache_p.fillRect(0, groove_y, track_w, groove_h, timeline_colors[(int)TimelineType::None]);

    cache_p.setRenderHint(QPainter::Antialiasing, false);
    drawEvents(cache_p, groove_y, groove_h, scale);
    drawUnloadedOverlay(cache_p, groove_y, groove_h, scale);
  }

  // Draw the full-width background color first
  p.fillRect(rect(), palette().window());

  // Draw the actual timeline track offset by margin
  p.drawPixmap(kMargin, 0, timeline_cache);

  // 2. Interactive Layers
  p.setRenderHint(QPainter::Antialiasing, true);

  // Hover Marker (Ghost Line)
  if (thumbnail_display_time >= 0) {
    double tx = kMargin + (thumbnail_display_time - min_time) * scale;
    p.setPen(Qt::NoPen);
    p.setBrush(palette().highlight());
    p.drawRoundedRect(QRectF(tx - 1, 0, 2, h), 1.0, 1.0);
  }

  drawScrubber(p, h, scale);
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
  const double handle_x = kMargin + (current_time - min_time) * scale;
  const QColor highlight = palette().color(QPalette::Highlight);

  // Needle
  p.setPen(QPen(QColor(0, 0, 0, 80), 3));
  p.drawLine(QPointF(handle_x, 0), QPointF(handle_x, h));
  p.setPen(QPen(highlight, 1));
  p.drawLine(QPointF(handle_x, 0), QPointF(handle_x, h));

  // Handle
  const double radius = (is_hovered || is_scrubbing) ? 8 : 7.0;
  const QPointF center(handle_x, h / 2.0);

  p.setPen(QPen(highlight, 1.5));
  p.setBrush(palette().color(QPalette::Button));
  p.drawEllipse(center, radius, radius);

  // Center Dot
  p.setBrush(palette().color(QPalette::WindowText));
  p.setPen(Qt::NoPen);
  p.drawEllipse(center, 1.5, 1.5);
}

void TimelineSlider::handleMouse(int x) {
  const int track_w = width() - (kMargin * 2);
  if (track_w <= 0) return;

  double t = min_time + ((double)(x - kMargin) / track_w) * (max_time - min_time);
  double seek_to = std::clamp(t, min_time, max_time);

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
  const int track_w = width() - (kMargin * 2);
  double t = min_time + ((double)(e->x() - kMargin) / track_w) * (max_time - min_time);
  setThumbnailTime(std::clamp(t, min_time, max_time));

  double handle_x = kMargin + (current_time - min_time) * ((double)track_w / (max_time - min_time));
  bool near = std::abs(e->pos().x() - handle_x) < 20;
  if (near != is_hovered) {
    is_hovered = near;
    update();
  }

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
