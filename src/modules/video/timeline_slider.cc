#include "timeline_slider.h"

#include <QMouseEvent>
#include <QPainter>

#include "core/streams/replay_stream.h"
#include "modules/system/stream_manager.h"
#include "playback_view.h"
#include "replay/include/timeline.h"

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
  if (current_time != t && !is_scrubbing) {
    current_time = t;
    update();
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
  // Only repaint the area that actually needs it
  p.fillRect(ev->rect(), palette().window());

  const double range = max_time - min_time;
  if (range <= 0 || width() <= 0) return;

  // Cache commonly used values
  const double scale = width() / range;
  if (timeline_cache.size() != size()) {
    updateCache();
  }
  p.drawPixmap(ev->rect(), timeline_cache, ev->rect());

  // 3. Interactive Layers - Enable AA for the diagonal lines of the playhead
  p.setRenderHint(QPainter::Antialiasing, true);
  drawMarkers(p, height(), scale);
  drawScrubber(p, height(), scale);
}

void TimelineSlider::drawEvents(QPainter& p, int y, int h, double scale) {
  auto replay = getReplay();
  if (!replay) return;

  for (const auto& entry : *replay->getTimeline()) {
    if (entry.end_time < min_time || entry.start_time > max_time) continue;

    int x1 = std::max(0.0, (entry.start_time - min_time) * scale);
    int x2 = std::min((double)width(), (entry.end_time - min_time) * scale);

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
  const auto event_data = replay->getEventData();

  for (const auto& [n, _] : replay->route().segments()) {
    double start = n * 60.0;
    double end = start + 60.0;

    if (end > min_time && start < max_time && !replay->getEventData()->isSegmentLoaded(n)) {
      int x1 = std::max(0.0, (start - min_time) * scale);
      int x2 = std::min((double)width(), (end - min_time) * scale);
      p.fillRect(x1, y, x2 - x1, h, overlay);
    }
  }
}

void TimelineSlider::drawMarkers(QPainter& p, int h, double scale) {
  if (thumbnail_display_time < 0) return;

  int tx = (thumbnail_display_time - min_time) * scale;
  p.setPen(Qt::NoPen);
  p.setBrush(palette().highlight());
  p.drawRoundedRect(QRect(tx - 1, 0, 2, h), 1.0, 1.0);
}

void TimelineSlider::drawScrubber(QPainter& p, int h, double scale) {
  const double handle_x = (current_time - min_time) * scale;
  const QColor highlight = palette().color(QPalette::Highlight);

  // 1. Needle: Dark shadow backdrop + highlight core
  p.setPen(QPen(QColor(0, 0, 0, 80), 3));
  p.drawLine(QPointF(handle_x, 0), QPointF(handle_x, h));
  p.setPen(QPen(highlight, 1));
  p.drawLine(QPointF(handle_x, 0), QPointF(handle_x, h));

  // 2. Handle: Circle grows on hover/scrub for better affordance
  const double radius = (is_hovered || is_scrubbing) ? 8 : 7.0;
  const QPointF center(handle_x, h / 2.0);

  p.setPen(QPen(highlight, 1.5));
  p.setBrush(palette().color(QPalette::Button));
  p.drawEllipse(center, radius, radius);

  // 3. Center Dot: High-contrast theme-aware indicator
  p.setBrush(palette().color(QPalette::WindowText));
  p.setPen(Qt::NoPen);
  p.drawEllipse(center, 1.5, 1.5);
}

void TimelineSlider::handleMouse(int x) {
  const double range = max_time - min_time;
  if (range <= 0 || width() <= 0) return;

  double seek_to = min_time + (x / (double)width()) * range;
  seek_to = std::clamp(seek_to, min_time, max_time);

  if (std::abs(seek_to - last_sent_seek_time) > 0.1 || !is_scrubbing) {
    StreamManager::stream()->seekTo(seek_to);
    last_sent_seek_time = seek_to;
  }

  // Update UI immediately for smoothness even if we don't seek the stream yet
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
  const double range = max_time - min_time;
  if (range <= 0 || width() <= 0) return;

  // 1. Update Ghost Line
  double hover_time = min_time + (e->x() / (double)width()) * range;
  setThumbnailTime(std::clamp(hover_time, min_time, max_time));

  // 2. Update Hover State (20px proximity check)
  double handle_x = (current_time - min_time) * (width() / range);
  bool near = std::abs(e->pos().x() - handle_x) < 20;
  if (near != is_hovered) {
    is_hovered = near;
    update();
  }

  // 3. Handle Dragging
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

void TimelineSlider::leaveEvent(QEvent* e) {
  is_hovered = false;
  setThumbnailTime(-1);
  QWidget::leaveEvent(e);
}

void TimelineSlider::updateCache() {
  timeline_cache = QPixmap(size());
  timeline_cache.fill(palette().window().color());
  QPainter cache_painter(&timeline_cache);

  const double range = max_time - min_time;
  const double scale = width() / range;
  const int groove_h = 6;
  const int groove_y = (height() - groove_h) / 2;

  cache_painter.fillRect(rect(), palette().window());
  cache_painter.fillRect(0, groove_y, width(), groove_h, timeline_colors[(int)TimelineType::None]);

  cache_painter.setRenderHint(QPainter::Antialiasing, false);
  drawEvents(cache_painter, groove_y, groove_h, scale);
  drawUnloadedOverlay(cache_painter, groove_y, groove_h, scale);
  update();
}