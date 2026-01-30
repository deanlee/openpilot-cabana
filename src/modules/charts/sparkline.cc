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
    updateRenderPoints(time_range, size);
    render();
  }
}

void Sparkline::updateDataPoints(const dbc::Signal* sig, CanEventIter first, CanEventIter last) {
if (first == last) return;

  uint64_t first_ts = (*first)->mono_ns;
  uint64_t last_ts = (*(last - 1))->mono_ns;

  bool is_backwards = !history_.empty() && last_ts < history_.back().mono_ns;
  bool is_far_forward = !history_.empty() && first_ts > (history_.back().mono_ns + 1000000000ULL);

  if (history_.empty() || is_backwards || is_far_forward) {
    clearHistory();
  }

  // Incremental update
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

void Sparkline::updateRenderPoints(int time_range, QSize size) {
  const qreal dpr = qApp->devicePixelRatio();
  if (image.size() != size * dpr) {
    image = QImage(size * dpr, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
  }
  image.fill(Qt::transparent);

  if (history_.empty()) return;

  const int width = size.width();
  const int height = size.height();
  const uint64_t range_ns = static_cast<uint64_t>(time_range) * 1000000000ULL;

  // 1. Efficiency & Clipping Constants
  const float pad = 2.0f; // Padding to prevent 3px head dot from clipping
  const float effective_w = std::max(1.0f, (float)width - (2.0f * pad));
  const float effective_h = std::max(1.0f, (float)height - (2.0f * pad));

  // 2. Padding-Aware ns_per_pixel
  const uint64_t ns_per_pixel = std::max<uint64_t>(1, range_ns / static_cast<uint64_t>(effective_w));
  const uint64_t window_end_ts = (current_window_max_ts_ / ns_per_pixel) * ns_per_pixel;
  const uint64_t window_start_ts = (window_end_ts > range_ns) ? (window_end_ts - range_ns) : 0;

  // 3. Min/Max Calculation
  calculateValueBounds();

  const float y_scale = effective_h / static_cast<float>(max_val - min_val);
  auto toY = [&](double v) {
    return (float)height - pad - (float)((v - min_val) * y_scale);
  };

  render_pts_.clear();
  render_pts_.reserve(width * 4);

  int current_x = -1;
  double b_entry, b_exit, b_min, b_max;
  uint64_t b_min_ts, b_max_ts;
  constexpr float VISUAL_EPS = 0.01f;

  auto flush_bucket = [&](int x) {
    if (x == -1) return;
    if (std::abs(b_min - b_max) < VISUAL_EPS) {
      if (!render_pts_.empty() && std::abs(render_pts_.back().y() - (float)b_min) > VISUAL_EPS) {
        render_pts_.emplace_back(x, render_pts_.back().y());
      }
      render_pts_.emplace_back(x, (float)b_min);
    } else {
      render_pts_.emplace_back(x, (float)b_entry);
      if (b_min_ts < b_max_ts) {
        render_pts_.emplace_back(x, (float)b_min); render_pts_.emplace_back(x, (float)b_max);
      } else {
        render_pts_.emplace_back(x, (float)b_max); render_pts_.emplace_back(x, (float)b_min);
      }
      render_pts_.emplace_back(x, (float)b_exit);
    }
  };

  // 4. Main Mapping Loop
  const float x_end = (float)width - pad;

  for (int i = 0; i < history_.size(); ++i) {
    const auto& p = history_[i];
    if (p.mono_ns < window_start_ts) continue;

    int x;
    if (p.mono_ns >= window_end_ts) {
      x = static_cast<int>(x_end);
    } else {
      uint64_t diff = window_end_ts - p.mono_ns;
      x = static_cast<int>(x_end - (static_cast<float>(diff) / ns_per_pixel));
    }
    x = std::clamp(x, (int)pad, (int)x_end);

    if (x != current_x) {
      flush_bucket(current_x);
      current_x = x;
      b_entry = b_exit = b_min = b_max = toY(p.value);
      b_min_ts = b_max_ts = p.mono_ns;
    } else {
      double y = toY(p.value);
      b_exit = y;
      if (y > b_min) { b_min = y; b_min_ts = p.mono_ns; }
      if (y < b_max) { b_max = y; b_max_ts = p.mono_ns; }
    }
  }
  flush_bucket(current_x);
}

void Sparkline::render() {
  if (render_pts_.empty()) return;

  QPainter painter(&image);
  // Grid-snapped points look best with Aliasing OFF (sharp 1px vertical lines)
  painter.setRenderHint(QPainter::Antialiasing, false);

  QColor color = is_highlighted_ ? qApp->palette().color(QPalette::HighlightedText) : signal_->color;

  // Width 0 = 1px Cosmetic Pen
  QPen pen(color, 0);
  painter.setPen(pen);
  if (render_pts_.size() > 1) {
    painter.drawPolyline(render_pts_.data(), (int)render_pts_.size());
  }

  // 4. Use Antialiasing for the dot to prevent "jagged" clipping
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setBrush(color);
  painter.drawEllipse(render_pts_.back(), 1.5f, 1.5f);
}

void Sparkline::calculateValueBounds() {
  min_val = std::numeric_limits<double>::max();
  max_val = std::numeric_limits<double>::lowest();
  for (int i = 0; i < history_.size(); ++i) {
    const auto& v = history_[i].value;
    if (v < min_val) min_val = v;
    if (v > max_val) max_val = v;
  }
  // Ensure we don't divide by zero for flat signals
  if (std::abs(max_val - min_val) < 1e-9) {
    min_val -= 1.0;
    max_val += 1.0;
  }
}

void Sparkline::setHighlight(bool highlight) {
  if (is_highlighted_ != highlight) {
    is_highlighted_ = highlight;
    render();
  }
}

void Sparkline::clearHistory() {
  last_processed_mono_ns_ = 0;
  history_.clear();
  render_pts_.clear();
  current_window_max_ts_ = 0;
  image = QImage();
}
