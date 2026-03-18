#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <array>
#include <limits>
#include <vector>

#include "core/dbc/dbc_message.h"
#include "core/streams/abstract_stream.h"

// Fixed-size ring buffer for time-series data. Size 32768 supports 30s of 1kHz data.
template <typename T, size_t Capacity = 32768>
class RingBuffer {
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");
  static constexpr size_t kMask = Capacity - 1;

 public:
  void push_back(const T& item) {
    buffer_[head_++ & kMask] = item;
    if (size_ < Capacity) ++size_;
  }

  const T& operator[](size_t i) const { return buffer_[(head_ - size_ + i) & kMask]; }
  const T& front() const { return (*this)[0]; }
  const T& back() const { return (*this)[size_ - 1]; }

  void pop_front_n(size_t n) { size_ = (n >= size_) ? 0 : size_ - n; }
  void clear() { head_ = size_ = 0; }
  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

 private:
  std::array<T, Capacity> buffer_;
  size_t head_ = 0;
  size_t size_ = 0;
};

// Lightweight sparkline chart for signal visualization in the message list.
// Uses M4 downsampling to efficiently render high-frequency data.
class Sparkline {
 public:
  struct Sample {
    uint64_t ts;     // Timestamp in nanoseconds
    double value;
  };

  void update(const dbc::Signal* sig, CanEventIter first, CanEventIter last,
              uint64_t current_ns, int time_window_sec, const QSize& size);

  bool isEmpty() const { return image_.isNull(); }
  bool isUpToDate(uint64_t current_ns) const { return last_update_ns_ == current_ns; }
  void setHighlight(bool on);
  void clearHistory();

  const QImage& image() const { return image_; }
  double minVal() const { return bounds_.min; }
  double maxVal() const { return bounds_.max; }

 private:
  // Value range tracker with lazy recomputation
  struct Bounds {
    double min = std::numeric_limits<double>::max();
    double max = std::numeric_limits<double>::lowest();
    bool dirty = false;

    void expand(double v) {
      if (v < min) min = v;
      if (v > max) max = v;
    }
    void reset() {
      min = std::numeric_limits<double>::max();
      max = std::numeric_limits<double>::lowest();
      dirty = false;
    }
    double range() const { return max - min; }
    bool isFlat() const { return range() < 1e-9; }
  };

  // M4 bucket: tracks entry/exit values and extremes within a pixel column
  struct PixelBucket {
    double entry, exit;
    double lo, hi;
    uint64_t lo_ts, hi_ts;

    void init(double v, uint64_t ts) {
      entry = exit = lo = hi = v;
      lo_ts = hi_ts = ts;
    }

    void add(double v, uint64_t ts) {
      exit = v;
      if (v < lo) { lo = v; lo_ts = ts; }
      if (v > hi) { hi = v; hi_ts = ts; }
    }
  };

  // Coordinate mapping from time/value to pixel space
  struct ViewTransform {
    uint64_t start_ns = 0, end_ns = 0;
    float width = 0, height = 0;
    float pad = 2.0f;
    double px_per_ns = 0;
    double y_scale = 0;
    float y_base = 0;

    void configure(uint64_t current_ns, int window_sec, const QSize& size, const Bounds& bounds);
    float timeToX(uint64_t ts) const;
    float valueToY(double v, double min_val) const;
  };

  void ingestNewSamples(const dbc::Signal* sig, CanEventIter first, CanEventIter last);
  void purgeOldSamples();
  void recomputeBoundsIfDirty();
  void buildRenderPath();
  void renderToImage();

  void emitBucketPoints(int x, const PixelBucket& b);
  void addPoint(float x, float y);

  RingBuffer<Sample> history_;
  std::vector<QPointF> points_;
  Bounds bounds_;
  ViewTransform view_;

  QImage image_;
  QColor color_;
  uint64_t last_update_ns_ = 0;
  bool highlighted_ = false;
};
