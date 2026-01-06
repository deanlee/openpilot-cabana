#include "chart/sparkline.h"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <algorithm>
#include <limits>

void Sparkline::update(const cabana::Signal* sig, CanEventIter first, CanEventIter last, int time_range, QSize size) {
  signal_ = sig;
  if (first == last || size.isEmpty()) {
    pixmap = QPixmap();
    history_.clear();
    last_processed_mono_time_ = 0;
    return;
  }
  updateDataPoints(sig, first, last);
  updateRenderPoints(sig->color, time_range, size);
  render();
}

void Sparkline::updateDataPoints(const cabana::Signal* sig, CanEventIter first, CanEventIter last) {
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

  // Incremental update of history
  double val = 0.0;
  for (auto it = first; it != last; ++it) {
    uint64_t ts = (*it)->mono_time;

    // Skip messages already calculated
    if (ts <= last_processed_mono_time_) continue;

    if (sig->getValue((*it)->dat, (*it)->size, &val)) {
      history_.push_back({ts, val});
    }
    last_processed_mono_time_ = ts;
  }

  while (!history_.empty() && history_.front().mono_time < first_ts) {
    history_.pop_front();
  }
}

void Sparkline::updateRenderPoints(const QColor& color, int time_range, QSize size) {
  if (history_.empty() || size.isEmpty()) return;

  qreal dpr = qApp->devicePixelRatio();
  QSize pix_size = size * dpr;
  if (pixmap.size() != pix_size) {
    pixmap = QPixmap(pix_size);
    pixmap.setDevicePixelRatio(dpr);
  }
  pixmap.fill(Qt::transparent);

  const int width = size.width();
  const int height = size.height();
  const uint64_t range_ns = (uint64_t)time_range * 1000000000ULL;

  // Align window: Right edge is the latest data point
  const uint64_t window_end_ts = current_window_max_ts_;
  const uint64_t window_start_ts = (window_end_ts > range_ns) ? (window_end_ts - range_ns) : 0;

  cols_.assign(width, {std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(), false});
  min_val = std::numeric_limits<double>::max();
  max_val = std::numeric_limits<double>::lowest();

  for (const auto& p : history_) {
    if (p.mono_time < window_start_ts) continue;

    // Calculate X based on the user-set time range
    double time_from_end = (double)(window_end_ts - p.mono_time);
    double ratio = 1.0 - (time_from_end / (double)range_ns);

    int x = (int)(ratio * (width - 1));
    if (x < 0 || x >= width) continue;

    auto& col = cols_[x];
    col.min_val = std::min(col.min_val, p.value);
    col.max_val = std::max(col.max_val, p.value);
    col.has_data = true;

    min_val = std::min(min_val, p.value);
    max_val = std::max(max_val, p.value);
  }

  // 3. Render Point Generation
  render_pts_.clear();
  render_pts_.reserve(width * 2);

  const int margin = 2;
  const float bottom_y = (float)(height - margin);
  const double draw_area = (double)(height - 2 * margin);
  const double y_range = max_val - min_val;

  if (y_range < 1e-6) {
    float mid_y = height / 2.0f;
    for (int x = 0; x < width; ++x) {
      if (cols_[x].has_data) render_pts_.emplace_back(x, mid_y);
    }
  } else {
    // Pre-calculate inversion to use multiplication instead of division in the loop
    const double scale = draw_area / y_range;
    for (int x = 0; x < width; ++x) {
      const auto& col = cols_[x];
      if (!col.has_data) continue;

      float py_min = bottom_y - (float)((col.min_val - min_val) * scale);
      float py_max = bottom_y - (float)((col.max_val - min_val) * scale);

      render_pts_.emplace_back(x, py_min);
      if (std::abs(py_min - py_max) > 0.5f) {
        render_pts_.emplace_back(x, py_max);
      }
    }
  }
}

void Sparkline::render() {
  if (render_pts_.empty()) return;

  QPainter painter(&pixmap);

  float x_min = render_pts_.front().x();
  float x_max = render_pts_.back().x();
  float data_x_span = std::max(x_max - x_min, 1.0f);

  float points_per_pixel = (float)render_pts_.size() / data_x_span;

  // If we have more than 1.5 points per pixel, disable AA to keep the
  // line sharp and prevent the "blurry ghosting" effect of high-freq data.
  painter.setRenderHint(QPainter::Antialiasing, points_per_pixel < 1.5f);

  QColor color = is_highlighted_ ? qApp->palette().color(QPalette::HighlightedText) : signal_->color;

  if (render_pts_.size() > 1) {
    painter.setPen(QPen(color, 1));
    painter.drawPolyline(render_pts_.data(), (int)render_pts_.size());
  }

  // We only draw dots if the points are "Sparse" (e.g., more than 4 pixels apart on average)
  // AND if the total number of points isn't so high it kills performance.
  bool is_sparse = points_per_pixel < 0.25f;  // Less than 1 point every 4 pixels

  if (is_sparse && render_pts_.size() < 100) {
    painter.setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap));
    painter.drawPoints(render_pts_.data(), (int)render_pts_.size());
  } else {
    // Always draw a distinct "head" (the latest value)
    painter.setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap));
    painter.drawPoint(render_pts_.back());
  }
}

void Sparkline::setHighlight(bool highlight) {
  if (is_highlighted_ != highlight) {
    is_highlighted_ = highlight;
    render();
  }
}
