#include "sparkline.h"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <algorithm>
#include <limits>

void Sparkline::update(const dbc::Signal* sig, CanEventIter first, CanEventIter last, int time_range, QSize size) {
  signal_ = sig;
  if (first == last || size.isEmpty()) {
    clearHistory();
    return;
  }
  updateDataPoints(sig, first, last);
  if (!history_.empty()) {
    const qreal dpr = qApp->devicePixelRatio();
    QSize pixelSize = size * dpr;

    if (image.size() != pixelSize) {
      image = QImage(pixelSize, QImage::Format_ARGB32_Premultiplied);
      image.setDevicePixelRatio(dpr);
    }
    image.fill(Qt::transparent);

    mapHistoryToPoints(time_range, size);
    render();
  }
}

void Sparkline::updateDataPoints(const dbc::Signal* sig, CanEventIter first, CanEventIter last) {
  uint64_t first_ts = (*first)->mono_ns;
  uint64_t last_ts = (*(last - 1))->mono_ns;

  bool is_backwards = !history_.empty() && last_ts < history_.back().mono_ns;
  bool is_far_forward = !history_.empty() && first_ts > (history_.back().mono_ns + 1000000000ULL);

  if (history_.empty() || is_backwards || is_far_forward) {
    clearHistory();
  }

  double val = 0.0;
  for (auto it = first; it != last; ++it) {
    uint64_t ts = (*it)->mono_ns;

    // Skip data we already have if we are just appending
    if (ts <= last_processed_mono_ns_) continue;

    if (sig->getValue((*it)->dat, (*it)->size, &val)) {
      history_.push_back({ts, val});
    }
    last_processed_mono_ns_ = ts;
  }

  // Purge data older than the window
  while (!history_.empty() && history_.front().mono_ns < first_ts) {
    history_.pop_front();
  }
  current_window_max_ts_ = last_ts;
}

void Sparkline::mapHistoryToPoints(int time_range, QSize size) {
 render_pts_.clear();
  if (history_.empty()) return;

  auto p = getTransformationParams(time_range, size);
  bool is_flat = calculateValueBounds();

  if (is_flat) {
    mapFlatPath(p);
  } else {
    mapNoisyPath(p);
  }

  if (render_pts_.size() == 1) {
    render_pts_.insert(render_pts_.begin(), render_pts_[0] - QPointF(1.0f, 0));
  }
}

Sparkline::TransformParams Sparkline::getTransformationParams(int time_range, QSize size) {
  TransformParams p;
  p.pad = 2.0f;
  p.w = (float)size.width();
  p.h = (float)size.height();

  const float eff_w = std::max(1.0f, p.w - (2.0f * p.pad));
  const uint64_t range_ns = static_cast<uint64_t>(time_range) * 1000000000ULL;
  const uint64_t ns_per_pixel = std::max<uint64_t>(1, range_ns / static_cast<uint64_t>(eff_w));
  p.win_end = (current_window_max_ts_ / ns_per_pixel) * ns_per_pixel;
  p.win_start = (p.win_end > range_ns) ? (p.win_end - range_ns) : 0;

  p.px_per_ns = 1.0 / ns_per_pixel;
  p.base_x = p.w - p.pad;

  return p;
}

// O(1) Path: Simply draws a centered line across the visible data range
void Sparkline::mapFlatPath(const TransformParams& p) {
  uint64_t draw_start = std::max(p.win_start, history_.front().mono_ns);
  uint64_t draw_end   = std::min(p.win_end,   history_.back().mono_ns);

  if (draw_start <= draw_end) {
    float y = p.h * 0.5f; // Explicitly centered
    render_pts_.emplace_back(getX(draw_start, p), y);
    if (draw_start < draw_end) {
      render_pts_.emplace_back(getX(draw_end, p), y);
    }
  }
}

// O(N) Path: M4 Algorithm to reduce high-frequency data into pixel buckets
void Sparkline::mapNoisyPath(const TransformParams& p) {
  const float eff_h = std::max(1.0f, p.h - (2.0f * p.pad));
  const float y_scale = eff_h / (float)(max_val - min_val);
  const float base_y = p.h - p.pad;

  render_pts_.reserve((size_t)std::max(1.0f, p.w) * 2);

  size_t start_idx = findFirstVisibleIdx(p.win_start);
  int last_x = -1;
  Bucket b;

  for (size_t i = start_idx; i < history_.size(); ++i) {
    const auto& pt = history_[i];
    int x = (int)getX(pt.mono_ns, p);
    float y = base_y - (float)((pt.value - min_val) * y_scale);

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

  // Compiler-friendly min/max loop
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

void Sparkline::setHighlight(bool highlight) {
  if (is_highlighted_ != highlight) {
    is_highlighted_ = highlight;
    render();
  }
}

size_t Sparkline::findFirstVisibleIdx(uint64_t ts) {
  size_t low = 0, high = history_.size();
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (history_[mid].mono_ns < ts)
      low = mid + 1;
    else
      high = mid;
  }
  return low;
}

void Sparkline::clearHistory() {
  last_processed_mono_ns_ = 0;
  history_.clear();
  render_pts_.clear();
  current_window_max_ts_ = 0;
  image = QImage();
}
