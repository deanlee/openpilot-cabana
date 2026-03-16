#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <vector>

#include "core/dbc/dbc_message.h"
#include "core/streams/abstract_stream.h"

// Size 32768 supports 30s of 1000Hz data
template <typename T, size_t N = 32768>
class RingBuffer {
  static_assert((N & (N - 1)) == 0, "Size must be power of two");

 public:
  void push_back(const T& item) {
    buffer[head++ & (N - 1)] = item;
    if (count < N) count++;
  }
  const T& operator[](size_t i) const { return buffer[(head - count + i) & (N - 1)]; }
  const T& front() const { return (*this)[0]; }
  const T& back() const { return (*this)[count - 1]; }
  void pop_front_n(size_t n) { count = (n >= count) ? 0 : count - n; }
  void pop_front() {
    if (count > 0) count--;
  }
  void clear() {
    head = 0;
    count = 0;
  }
  size_t size() const { return count; }
  bool empty() const { return count == 0; }

 private:
  std::array<T, N> buffer;
  size_t head = 0;
  size_t count = 0;
};

class Sparkline {
 public:
  struct DataPoint {
    uint64_t mono_ns;
    double value;
  };
  void update(const dbc::Signal* sig, CanEventIter first, CanEventIter last,
              uint64_t current_ns, int time_window, const QSize& size);
  bool isEmpty() const { return image_.isNull(); }
  bool isUpToDate(uint64_t current_ns) const { return last_processed_ns_ == current_ns; }
  void setHighlight(bool highlight);
  void clearHistory();

  const QImage& image() const { return image_; }
  double minVal() const { return min_val_; }
  double maxVal() const { return max_val_; }

 private:
  static constexpr double kFlatnessEps = 1e-9;

  // Tracks per-pixel extremes in signal-value space (M4 downsampling algorithm).
  struct Bucket {
    double entry = 0.0, exit = 0.0;
    double val_min = std::numeric_limits<double>::max();
    double val_max = std::numeric_limits<double>::lowest();
    uint64_t val_min_ts = 0, val_max_ts = 0;

    void init(double value, uint64_t ts) {
      entry = exit = val_min = val_max = value;
      val_min_ts = val_max_ts = ts;
    }

    void update(double value, uint64_t ts) {
      exit = value;
      if (value < val_min) { val_min = value; val_min_ts = ts; }
      if (value > val_max) { val_max = value; val_max_ts = ts; }
    }
  };

  void prepareWindow(uint64_t current_ns, int time_window, const QSize& size);
  void updateDataPoints(const dbc::Signal* sig, CanEventIter first, CanEventIter last);
  void mapHistoryToPoints();
  void updateValueBounds();
  void flushBucket(int x, const Bucket& b, float base_y, double y_scale);
  void addUniquePoint(int x, float y);
  void render();
  void mapFlatPath();
  void mapNoisyPath();
  inline float getX(uint64_t ts) const {
    if (ts >= win_end_ns_) return right_edge_;
    float x = pad_ + static_cast<float>(static_cast<double>(ts - win_start_ns_) * px_per_ns_);
    return (x < pad_) ? pad_ : x;
  }

  static constexpr float pad_ = 2.0f;
  uint64_t win_start_ns_ = 0;
  uint64_t win_end_ns_ = 0;
  float right_edge_ = 0.0f;
  float px_per_ns_ = 0;
  QSize widget_size_;

  RingBuffer<DataPoint> history_;
  std::vector<QPointF> render_pts_;
  bool bounds_dirty_ = true;
  bool is_highlighted_ = false;
  uint64_t last_processed_ns_ = 0;
  QColor signal_color_;
  QImage image_;
  double min_val_ = std::numeric_limits<double>::max();
  double max_val_ = std::numeric_limits<double>::lowest();
};
