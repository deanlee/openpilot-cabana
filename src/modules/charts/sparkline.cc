#include "sparkline.h"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <algorithm>
#include <limits>

void Sparkline::update(const dbc::Signal* sig, CanEventIter first, CanEventIter last, int time_range, QSize size) {
  signal_ = sig;
  if (first == last || size.isEmpty()) {
    pixmap = QPixmap();
    history_.clear();
    last_processed_mono_time_ = 0;
    return;
  }
  updateDataPoints(sig, first, last);
  if (!history_.empty()) {
    updateRenderPoints(time_range, size);
    render();
  }
}

void Sparkline::updateDataPoints(const dbc::Signal* sig, CanEventIter first, CanEventIter last) {
  uint64_t first_ts = (*first)->mono_time;
  uint64_t last_ts = (*(last - 1))->mono_time;
  current_window_min_ts_ = first_ts;
  current_window_max_ts_ = last_ts;

  bool is_backwards_seek = last_ts < history_.front().mono_time;
  bool is_forwards_jump = first_ts > history_.back().mono_time;
  bool is_range_expansion = first_ts < history_.front().mono_time;

  if (history_.empty() || is_backwards_seek || is_forwards_jump || is_range_expansion) {
    history_.clear();
    last_processed_mono_time_ = 0;
  }

  // Incremental update
  double val = 0.0;
  for (auto it = first; it != last; ++it) {
    uint64_t ts = (*it)->mono_time;
    if (ts <= last_processed_mono_time_) continue;

    if (sig->getValue((*it)->dat, (*it)->size, &val)) {
      history_.push_back({ts, val});
    }
    last_processed_mono_time_ = ts;
  }

  // Purge data older than the window
  while (!history_.empty() && history_.front().mono_time < first_ts) {
    history_.pop_front();
  }
}
void Sparkline::updateRenderPoints(int time_range, QSize size) {
  if (history_.empty() || size.isEmpty()) return;

  const qreal dpr = qApp->devicePixelRatio();
  if (pixmap.size() != size * dpr) {
    pixmap = QPixmap(size * dpr);
    pixmap.setDevicePixelRatio(dpr);
  }
  pixmap.fill(Qt::transparent);

  const int width = size.width();
  const int height = size.height();
  const uint64_t range_ns = (uint64_t)time_range * 1000000000ULL;

  const uint64_t ns_per_pixel = std::max<uint64_t>(1, range_ns / width);
  const uint64_t window_end_ts = (current_window_max_ts_ / ns_per_pixel) * ns_per_pixel;
  const uint64_t window_start_ts = (window_end_ts > range_ns) ? (window_end_ts - range_ns) : 0;

  min_val = std::numeric_limits<double>::max();
  max_val = std::numeric_limits<double>::lowest();
  for (const auto& p : history_) {
    if (p.mono_time < window_start_ts) continue;
    min_val = std::min(min_val, p.value);
    max_val = std::max(max_val, p.value);
  }

  const double y_range = std::max(max_val - min_val, 1e-6);
  const float margin = 1.0f;
  const float y_scale = ((float)height - (2.0f * margin)) / (float)y_range;

  render_pts_.clear();
  render_pts_.reserve(width * 4);

  auto toY = [&](double v) {
    return (float)height - margin - (float)((v - min_val) * y_scale);
  };

  int current_x = -1;
  double b_entry, b_exit, b_min, b_max;
  uint64_t b_min_ts, b_max_ts;

  auto flush_bucket = [&](int x) {
    if (x == -1) return;

    bool is_flat = qFuzzyCompare(b_min, b_max);

    if (is_flat) {
      if (render_pts_.empty()) {
        render_pts_.emplace_back(x, b_min);
      } else {
        bool value_changed = !qFuzzyCompare(render_pts_.back().y(), b_min);
        if (value_changed) {
          // Add a step corner to keep lines rectilinear, then start the new flat segment
          render_pts_.emplace_back(x, render_pts_.back().y());
          render_pts_.emplace_back(x, b_min);
        } else {
          // Optimization: If we already have 2+ points at this Y, just move the end-cap
          if (render_pts_.size() >= 2 && qFuzzyCompare(render_pts_[render_pts_.size() - 2].y(), b_min)) {
            render_pts_.back().setX(x);
          } else {
            // Second point of a new flat segment
            render_pts_.emplace_back(x, b_min);
          }
        }
      }
    } else {
      // M4 Noisy Bucket
      render_pts_.emplace_back(x, b_entry);
      if (b_min_ts < b_max_ts) {
        render_pts_.emplace_back(x, b_min);
        render_pts_.emplace_back(x, b_max);
      } else {
        render_pts_.emplace_back(x, b_max);
        render_pts_.emplace_back(x, b_min);
      }
      render_pts_.emplace_back(x, b_exit);
    }
  };

  for (const auto& p : history_) {
    if (p.mono_time < window_start_ts || p.mono_time > window_end_ts) continue;

    int x = (width - 1) - static_cast<int>((window_end_ts - p.mono_time) / ns_per_pixel);
    x = std::clamp(x, 0, width - 1);

    if (x != current_x) {
      flush_bucket(current_x);
      current_x = x;
      b_entry = b_exit = b_min = b_max = toY(p.value);
      b_min_ts = b_max_ts = p.mono_time;
    } else {
      double y = toY(p.value);
      b_exit = y;
      if (y > b_min) {
        b_min = y;
        b_min_ts = p.mono_time;
      }
      if (y < b_max) {
        b_max = y;
        b_max_ts = p.mono_time;
      }
    }
  }
  flush_bucket(current_x);
}

void Sparkline::render() {
  if (render_pts_.empty()) return;

  QPainter painter(&pixmap);
  // Grid-snapped points look best with Aliasing OFF (sharp 1px vertical lines)
  painter.setRenderHint(QPainter::Antialiasing, false);

  QColor color = is_highlighted_ ? qApp->palette().color(QPalette::HighlightedText) : signal_->color;

  if (render_pts_.size() > 1) {
    painter.setPen(QPen(color, 1));
    painter.drawPolyline(render_pts_.data(), (int)render_pts_.size());
  }

  // Draw the "head" (current value) dot
  painter.setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap));
  painter.drawPoint(render_pts_.back());
}

void Sparkline::setHighlight(bool highlight) {
  if (is_highlighted_ != highlight) {
    is_highlighted_ = highlight;
    render();
  }
}
