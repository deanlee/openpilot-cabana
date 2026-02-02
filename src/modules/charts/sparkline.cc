#include "sparkline.h"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <algorithm>
#include <limits>

#include "modules/system/stream_manager.h"

bool SparklineContext::update(const MessageId& msg_id, uint64_t current_ns, int time_window, const QSize& size) {
  const uint64_t range_ns = static_cast<uint64_t>(time_window) * 1000000000ULL;
  const float w = static_cast<float>(size.width());
  const float eff_w = std::max(1.0f, w - (2.0f * pad));

  const double ns_per_px_dbl = std::max(1.0, static_cast<double>(range_ns) / eff_w);
  const uint64_t step = static_cast<uint64_t>(ns_per_px_dbl);

  // Snap win_end_ns to pixel boundaries
  const uint64_t new_win_end_ns = (current_ns / step) * step;

  // If size hasn't changed and time hasn't crossed a pixel boundary, skip.
  bool size_changed = (size != widget_size);
  bool time_shifted = (new_win_end_ns != win_end_ns);

  // Jump Detection
  jump_detected = (last_processed_mono_ns != 0) &&
                  ((current_ns < last_processed_mono_ns) ||
                   (current_ns > last_processed_mono_ns + 1000000000ULL));

  if (!size_changed && !time_shifted && !jump_detected) {
    first = last; // Signals to the caller that no new processing is needed
    return false;
  }

  // Commit updates
  win_end_ns = new_win_end_ns;
  win_start_ns = (win_end_ns > range_ns) ? (win_end_ns - range_ns) : 0;
  widget_size = size;
  right_edge = w - pad;
  px_per_ns = 1.0 / ns_per_px_dbl;

  uint64_t fetch_start = jump_detected ? win_start_ns : (last_processed_mono_ns + 1);

  // Safety check: if we are caught up
  if (fetch_start > win_end_ns && !jump_detected) {
    first = last;
    return false;
  }

  auto* stream = StreamManager::stream();
  auto range = stream->eventsInRange(msg_id, std::make_pair(stream->toSeconds(fetch_start), stream->toSeconds(win_end_ns)));

  first = range.first;
  last = range.second;

  if (first != last) {
    last_processed_mono_ns = (*(last - 1))->mono_ns;
  } else if (jump_detected) {
    last_processed_mono_ns = win_end_ns;
  }
  return true;
}

void Sparkline::update(const dbc::Signal* sig, const SparklineContext& ctx) {
  signal_ = sig;
  if (ctx.jump_detected) history_.clear();

  updateDataPoints(sig, ctx);

  if (!history_.empty()) {
    const qreal dpr = qApp->devicePixelRatio();
    QSize pixelSize = ctx.widget_size * dpr;

    if (image.size() != pixelSize) {
      image = QImage(pixelSize, QImage::Format_ARGB32_Premultiplied);
      image.setDevicePixelRatio(dpr);
    }
    image.fill(Qt::transparent);

    mapHistoryToPoints(ctx);
    render();
  }
}

void Sparkline::updateDataPoints(const dbc::Signal* sig, const SparklineContext& ctx) {
  double val = 0.0;
  for (auto it = ctx.first; it != ctx.last; ++it) {
    auto *e = *it;
    if (sig->getValue(e->dat, e->size, &val)) {
      history_.push_back({e->mono_ns, val});
    }
  }

  // Purge data older than the window
  // Keep ONE point just outside win_start_ns for smooth edge rendering
  while (history_.size() > 1 && history_.front().mono_ns < ctx.win_start_ns) {
    history_.pop_front();
  }
}

void Sparkline::mapHistoryToPoints(const SparklineContext& ctx) {
  render_pts_.clear();

  bool is_flat = calculateValueBounds();

  if (is_flat) {
    mapFlatPath(ctx);
  } else {
    mapNoisyPath(ctx);
  }

  if (render_pts_.size() == 1) {
    render_pts_.insert(render_pts_.begin(), render_pts_[0] - QPointF(1.0f, 0));
  }
}

// O(1) Path: Simply draws a centered line across the visible data range
void Sparkline::mapFlatPath(const SparklineContext& ctx) {
  uint64_t draw_start = std::max(ctx.win_start_ns, history_.front().mono_ns);
  uint64_t draw_end = std::min(ctx.win_end_ns, history_.back().mono_ns);

  if (draw_start <= draw_end) {
    float y = ctx.widget_size.height() * 0.5f;  // Explicitly centered
    render_pts_.emplace_back(ctx.getX(draw_start), y);
    if (draw_start < draw_end) {
      render_pts_.emplace_back(ctx.getX(draw_end), y);
    }
  }
}

// O(N) Path: M4 Algorithm to reduce high-frequency data into pixel buckets
void Sparkline::mapNoisyPath(const SparklineContext& ctx) {
  const float eff_h = std::max(1.0f, ctx.widget_size.height() - (2.0f * ctx.pad));
  const double y_scale = static_cast<double>(eff_h) / (max_val - min_val);
  const float base_y = ctx.widget_size.height() - ctx.pad;

  size_t target_size = (size_t)ctx.widget_size.width() * 2;
  if (render_pts_.capacity() < target_size) render_pts_.reserve(target_size);

  int last_x = -1;
  Bucket b;

  for (size_t i = 0; i < history_.size(); ++i) {
    const auto& pt = history_[i];
    int x = static_cast<int>(ctx.getX(pt.mono_ns));
    float y = base_y - static_cast<float>((pt.value - min_val) * y_scale);

    if (x != last_x) {
      if (last_x != -1) flushBucket(last_x, b);
      b.init(y, pt.mono_ns);
      last_x = x;
    } else {
      b.update(y, pt.mono_ns);
    }
  }
  flushBucket(last_x, b);
}

void Sparkline::flushBucket(int x, const Bucket& b) {
  if (x == -1) return;

  // M4 Algorithm: Entry -> Min/Max -> Exit
  // This preserves visual extrema within a single pixel column
  addUniquePoint(x, b.entry);
  if (b.min_ts != b.max_ts) {
    if (b.min_ts < b.max_ts) {
      addUniquePoint(x, b.min);
      addUniquePoint(x, b.max);
    } else {
      addUniquePoint(x, b.max);
      addUniquePoint(x, b.min);
    }
  }
  addUniquePoint(x, b.exit);
}

void Sparkline::addUniquePoint(int x, float y) {
  if (!render_pts_.empty()) {
    auto& last = render_pts_.back();

    // If the new point is visually identical to the last one, skip it.
    constexpr float EPS = 0.1f;
    bool same_y = std::abs(last.y() - y) < EPS;

    if ((int)last.x() == x && same_y) return;

    // Horizontal Segment Collapsing
    // If P_prev, P_last, and P_new form a flat line, we just move P_last to P_new's X.
    if (render_pts_.size() >= 2) {
      const auto& prev = render_pts_[render_pts_.size() - 2];
      bool prev_flat = std::abs(prev.y() - last.y()) < EPS;

      if (prev_flat && same_y) {
        last.setX(x);  // Stretch the line
        return;
      }
    }
  }
  render_pts_.emplace_back(x, y);
}

void Sparkline::render() {
  if (render_pts_.empty()) return;

  QPainter p(&image);
  const QColor color = is_highlighted_ ? qApp->palette().color(QPalette::HighlightedText) : signal_->color;

  // Line Rendering: Aliasing OFF for crisp 1px lines
  p.setRenderHint(QPainter::Antialiasing, false);
  p.setPen(QPen(color, 0));  // 0 = Hairline cosmetic pen

  if (render_pts_.size() > 1) {
    p.drawPolyline(render_pts_.data(), (int)render_pts_.size());
  }

  // Endpoint Dot: Aliasing ON for smoothness
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setBrush(color);
  p.drawEllipse(render_pts_.back(), 1.5, 1.5);
}

bool Sparkline::calculateValueBounds() {
  if (history_.empty()) return true;

  double current_min = history_[0].value;
  double current_max = history_[0].value;

  for (size_t i = 1; i < history_.size(); ++i) {
    const double v = history_[i].value;
    if (v < current_min) current_min = v;
    if (v > current_max) current_max = v;
  }

  min_val = current_min;
  max_val = current_max;

  const bool flat = (std::abs(max_val - min_val) < 1e-9);

  // Prevent division by zero in scaling logic later
  if (flat) {
    min_val -= 1.0;
    max_val += 1.0;
  }
  return flat;
}

void Sparkline::clearHistory() {
  history_.clear();
  render_pts_.clear();
  image = QImage();
}

void Sparkline::setHighlight(bool highlight) {
  if (is_highlighted_ != highlight) {
    is_highlighted_ = highlight;
    render();
  }
}
