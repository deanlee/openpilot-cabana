#include "sparkline.h"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <algorithm>
#include <limits>

void Sparkline::prepareWindow(uint64_t current_ns, int time_window, const QSize& size) {
  const uint64_t range_ns = static_cast<uint64_t>(time_window) * 1000000000ULL;
  const float w = static_cast<float>(size.width());
  const float eff_w = std::max(1.0f, w - (2.0f * pad_));
  const double ns_per_px = std::max(1.0, static_cast<double>(range_ns) / eff_w);
  const uint64_t step = static_cast<uint64_t>(ns_per_px);

  win_end_ns_ = current_ns;
  win_start_ns_ = (current_ns > range_ns) ? (current_ns - range_ns) : 0;
  win_start_ns_ = (win_start_ns_ / step) * step;
  widget_size_ = size;
  right_edge_ = w - pad_;
  px_per_ns_ = 1.0 / ns_per_px;
}

void Sparkline::update(const dbc::Signal* sig, CanEventIter first, CanEventIter last,
                       uint64_t current_ns, int time_window, const QSize& size) {
  signal_color_ = sig->color;
  prepareWindow(current_ns, time_window, size);
  updateDataPoints(sig, first, last);
  last_processed_ns_ = win_end_ns_;

  if (!history_.empty()) {
    const qreal dpr = qApp->devicePixelRatio();
    QSize pixelSize = widget_size_ * dpr;

    if (image_.size() != pixelSize) {
      image_ = QImage(pixelSize, QImage::Format_ARGB32_Premultiplied);
      image_.setDevicePixelRatio(dpr);
    }
    image_.fill(Qt::transparent);

    mapHistoryToPoints();
    render();
  }
}

void Sparkline::updateDataPoints(const dbc::Signal* sig, CanEventIter first, CanEventIter last) {
  // Skip events already processed by this sparkline
  auto it = std::lower_bound(first, last, last_processed_ns_ + 1,
                             [](const CanEvent* e, uint64_t ns) { return e->mono_ns < ns; });

  double val = 0.0;
  for (; it != last; ++it) {
    auto* e = *it;
    if (sig->parse(e->dat, e->size, &val)) {
      history_.push_back({e->mono_ns, val});
      // Update running bounds
      if (val < min_val_) min_val_ = val;
      if (val > max_val_) max_val_ = val;
    }
  }

  // Purge data older than the window
  // Keep ONE point just outside win_start_ns_ for smooth edge rendering
  size_t purge_count = 0;
  while (history_.size() - purge_count > 1 && history_[purge_count].mono_ns < win_start_ns_) {
    purge_count++;
  }
  if (purge_count > 0) {
    history_.pop_front_n(purge_count);
    bounds_dirty_ = true;  // Purged points may have held min/max
  }
}

void Sparkline::mapHistoryToPoints() {
  render_pts_.clear();

  updateValueBounds();
  const bool is_flat = (max_val_ - min_val_) < kFlatnessEps;

  if (is_flat) {
    mapFlatPath();
  } else {
    mapNoisyPath();
  }

  if (render_pts_.size() == 1) {
    render_pts_.push_back(render_pts_[0]);
    render_pts_[0].setX(render_pts_[1].x() - 1.0);
  }
}

// O(1) Path: Simply draws a centered line across the visible data range
void Sparkline::mapFlatPath() {
  uint64_t draw_start = std::max(win_start_ns_, history_.front().mono_ns);
  uint64_t draw_end = std::min(win_end_ns_, history_.back().mono_ns);

  if (draw_start <= draw_end) {
    float y = widget_size_.height() * 0.5f;  // Explicitly centered
    render_pts_.emplace_back(getX(draw_start), y);
    if (draw_start < draw_end) {
      render_pts_.emplace_back(getX(draw_end), y);
    }
  }
}

// O(N) Path: M4 Algorithm to reduce high-frequency data into pixel buckets.
// Bucket tracks extremes in signal-value space; canvas-Y conversion happens in flushBucket.
void Sparkline::mapNoisyPath() {
  const float eff_h = std::max(1.0f, widget_size_.height() - (2.0f * pad_));
  const double y_scale = static_cast<double>(eff_h) / (max_val_ - min_val_);
  const float base_y = widget_size_.height() - pad_;

  size_t max_expected_points = static_cast<size_t>(widget_size_.width()) * 4;
  if (render_pts_.capacity() < max_expected_points) render_pts_.reserve(max_expected_points);

  int last_x = -1;
  Bucket b;

  for (size_t i = 0; i < history_.size(); ++i) {
    const auto& pt = history_[i];
    int x = static_cast<int>(getX(pt.mono_ns));

    if (x != last_x) {
      if (last_x != -1) {
        flushBucket(last_x, b, base_y, y_scale);
      }
      b.init(pt.value, pt.mono_ns);
      last_x = x;
    } else {
      b.update(pt.value, pt.mono_ns);
    }
  }
  flushBucket(last_x, b, base_y, y_scale);

  // Pin the very last point to the right edge to prevent the line from stopping
  // short due to pixel-bucket alignment.
  if (!history_.empty()) {
    const double last_value = history_.back().value;
    float final_y = base_y - static_cast<float>((last_value - min_val_) * y_scale);
    addUniquePoint(static_cast<int>(right_edge_), final_y);
  }
}

void Sparkline::flushBucket(int x, const Bucket& b, float base_y, double y_scale) {
  auto toY = [&](double value) -> float {
    return base_y - static_cast<float>((value - min_val_) * y_scale);
  };

  addUniquePoint(x, toY(b.entry));

  // Emit val_min and val_max in chronological order to preserve the visual shape.
  if (b.val_min_ts < b.val_max_ts) {
    if (b.val_min != b.entry && b.val_min != b.exit) addUniquePoint(x, toY(b.val_min));
    if (b.val_max != b.entry && b.val_max != b.exit) addUniquePoint(x, toY(b.val_max));
  } else {
    if (b.val_max != b.entry && b.val_max != b.exit) addUniquePoint(x, toY(b.val_max));
    if (b.val_min != b.entry && b.val_min != b.exit) addUniquePoint(x, toY(b.val_min));
  }

  addUniquePoint(x, toY(b.exit));
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

  QPainter p(&image_);
  const QColor color = is_highlighted_ ? qApp->palette().color(QPalette::HighlightedText) : signal_color_;

  if (render_pts_.size() > 1) {
    // Line Rendering: Aliasing OFF for crisp 1px lines
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(color, 0));  // 0 = Hairline cosmetic pen
    p.drawPolyline(render_pts_.data(), (int)render_pts_.size());
  }

  // Endpoint Dot: Aliasing ON for smoothness
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(Qt::NoPen);
  p.setBrush(color);
  p.drawEllipse(render_pts_.back(), 1.5, 1.5);
}

void Sparkline::updateValueBounds() {
  if (!bounds_dirty_) return;
  bounds_dirty_ = false;

  if (history_.empty()) {
    min_val_ = std::numeric_limits<double>::max();
    max_val_ = std::numeric_limits<double>::lowest();
    return;
  }

  double lo = history_[0].value;
  double hi = lo;
  for (size_t i = 1; i < history_.size(); ++i) {
    const double v = history_[i].value;
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  min_val_ = lo;
  max_val_ = hi;
}

void Sparkline::clearHistory() {
  history_.clear();
  render_pts_.clear();
  image_ = QImage();
  min_val_ = std::numeric_limits<double>::max();
  max_val_ = std::numeric_limits<double>::lowest();
  bounds_dirty_ = false;
  last_processed_ns_ = 0;
}

void Sparkline::setHighlight(bool highlight) {
  if (is_highlighted_ == highlight) return;
  is_highlighted_ = highlight;
  if (!render_pts_.empty()) render();
}
