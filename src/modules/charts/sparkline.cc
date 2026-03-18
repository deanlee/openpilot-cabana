#include "sparkline.h"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <algorithm>
#include <cmath>

// ViewTransform implementation

void Sparkline::ViewTransform::configure(uint64_t current_ns, int window_sec, const QSize& size,
                                         const Bounds& bounds) {
  const uint64_t range_ns = static_cast<uint64_t>(window_sec) * 1'000'000'000ULL;
  const float usable_w = std::max(1.0f, size.width() - 2.0f * pad);
  const float usable_h = std::max(1.0f, size.height() - 2.0f * pad);
  const double ns_per_px = std::max(1.0, static_cast<double>(range_ns) / usable_w);
  const uint64_t step = static_cast<uint64_t>(ns_per_px);

  end_ns = current_ns;
  start_ns = (current_ns > range_ns) ? (current_ns - range_ns) : 0;
  start_ns = (start_ns / step) * step;  // Align to pixel boundary

  width = static_cast<float>(size.width());
  height = static_cast<float>(size.height());
  px_per_ns = 1.0 / ns_per_px;

  // Y-axis transform (value -> pixel)
  y_base = height - pad;
  y_scale = bounds.isFlat() ? 0.0 : usable_h / bounds.range();
}

float Sparkline::ViewTransform::timeToX(uint64_t ts) const {
  if (ts >= end_ns) return width - pad;
  float x = pad + static_cast<float>(static_cast<double>(ts - start_ns) * px_per_ns);
  return std::max(pad, x);
}

float Sparkline::ViewTransform::valueToY(double v, double min_val) const {
  return y_base - static_cast<float>((v - min_val) * y_scale);
}

// Sparkline implementation

void Sparkline::update(const dbc::Signal* sig, CanEventIter first, CanEventIter last,
                       uint64_t current_ns, int time_window_sec, const QSize& size) {
  color_ = sig->color;

  ingestNewSamples(sig, first, last);
  recomputeBoundsIfDirty();
  view_.configure(current_ns, time_window_sec, size, bounds_);
  purgeOldSamples();

  last_update_ns_ = current_ns;

  if (history_.empty()) return;

  // Prepare image buffer
  const qreal dpr = qApp->devicePixelRatio();
  QSize pixel_size = size * dpr;
  if (image_.size() != pixel_size) {
    image_ = QImage(pixel_size, QImage::Format_ARGB32_Premultiplied);
    image_.setDevicePixelRatio(dpr);
  }
  image_.fill(Qt::transparent);

  buildRenderPath();
  renderToImage();
}

void Sparkline::ingestNewSamples(const dbc::Signal* sig, CanEventIter first, CanEventIter last) {
  // Skip events already processed
  auto it = std::lower_bound(first, last, last_update_ns_ + 1,
                             [](const CanEvent* e, uint64_t ns) { return e->mono_ns < ns; });

  double val = 0.0;
  for (; it != last; ++it) {
    const auto* e = *it;
    if (sig->parse(e->dat, e->size, &val)) {
      history_.push_back({e->mono_ns, val});
      bounds_.expand(val);
    }
  }
}

void Sparkline::purgeOldSamples() {
  // Keep one sample before window start for smooth edge rendering
  size_t purge = 0;
  while (history_.size() - purge > 1 && history_[purge].ts < view_.start_ns) {
    ++purge;
  }
  if (purge > 0) {
    history_.pop_front_n(purge);
    bounds_.dirty = true;
  }
}

void Sparkline::recomputeBoundsIfDirty() {
  if (!bounds_.dirty) return;
  bounds_.reset();

  for (size_t i = 0; i < history_.size(); ++i) {
    bounds_.expand(history_[i].value);
  }
}

void Sparkline::buildRenderPath() {
  points_.clear();

  if (bounds_.isFlat()) {
    // Flat signal: draw horizontal line across visible range
    uint64_t t0 = std::max(view_.start_ns, history_.front().ts);
    uint64_t t1 = std::min(view_.end_ns, history_.back().ts);
    if (t0 <= t1) {
      float y = view_.height * 0.5f;
      points_.emplace_back(view_.timeToX(t0), y);
      if (t0 < t1) points_.emplace_back(view_.timeToX(t1), y);
    }
  } else {
    // M4 downsampling: bucket samples by pixel column
    points_.reserve(static_cast<size_t>(view_.width) * 4);

    PixelBucket bucket;
    int prev_x = -1;

    for (size_t i = 0; i < history_.size(); ++i) {
      const auto& s = history_[i];
      int x = static_cast<int>(view_.timeToX(s.ts));

      if (x != prev_x) {
        if (prev_x >= 0) emitBucketPoints(prev_x, bucket);
        bucket.init(s.value, s.ts);
        prev_x = x;
      } else {
        bucket.add(s.value, s.ts);
      }
    }
    if (prev_x >= 0) emitBucketPoints(prev_x, bucket);

    // Pin final point to right edge
    if (!history_.empty()) {
      float final_y = view_.valueToY(history_.back().value, bounds_.min);
      addPoint(view_.width - view_.pad, final_y);
    }
  }

  // Ensure at least 2 points for polyline
  if (points_.size() == 1) {
    points_.push_back(points_[0]);
    points_[0].setX(points_[1].x() - 1.0f);
  }
}

void Sparkline::emitBucketPoints(int x, const PixelBucket& b) {
  auto toY = [&](double v) { return view_.valueToY(v, bounds_.min); };
  float fx = static_cast<float>(x);

  addPoint(fx, toY(b.entry));

  // Emit min/max in chronological order to preserve waveform shape
  bool lo_first = (b.lo_ts < b.hi_ts);
  double first_extreme = lo_first ? b.lo : b.hi;
  double second_extreme = lo_first ? b.hi : b.lo;

  if (first_extreme != b.entry && first_extreme != b.exit) addPoint(fx, toY(first_extreme));
  if (second_extreme != b.entry && second_extreme != b.exit) addPoint(fx, toY(second_extreme));

  addPoint(fx, toY(b.exit));
}

void Sparkline::addPoint(float x, float y) {
  constexpr float kEps = 0.1f;

  if (!points_.empty()) {
    auto& last = points_.back();
    bool same_y = std::abs(last.y() - y) < kEps;

    // Skip visually identical points
    if (static_cast<int>(last.x()) == static_cast<int>(x) && same_y) return;

    // Collapse horizontal segments: if prev->last->new are all on same Y, just extend
    if (points_.size() >= 2 && same_y) {
      const auto& prev = points_[points_.size() - 2];
      if (std::abs(prev.y() - last.y()) < kEps) {
        last.setX(x);
        return;
      }
    }
  }
  points_.emplace_back(x, y);
}

void Sparkline::renderToImage() {
  if (points_.empty()) return;

  QPainter p(&image_);
  const QColor c = highlighted_ ? qApp->palette().color(QPalette::HighlightedText) : color_;

  // Draw polyline (no anti-aliasing for crisp 1px lines)
  if (points_.size() > 1) {
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(c, 0));
    p.drawPolyline(points_.data(), static_cast<int>(points_.size()));
  }

  // Draw endpoint dot (with anti-aliasing for smoothness)
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(Qt::NoPen);
  p.setBrush(c);
  p.drawEllipse(points_.back(), 1.5, 1.5);
}

void Sparkline::clearHistory() {
  history_.clear();
  points_.clear();
  image_ = QImage();
  bounds_.reset();
  last_update_ns_ = 0;
}

void Sparkline::setHighlight(bool on) {
  if (highlighted_ == on) return;
  highlighted_ = on;
  if (!points_.empty()) renderToImage();
}
