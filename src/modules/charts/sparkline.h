#pragma once

#include <QImage>
#include <QPointF>
#include <deque>
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
  bool isEmpty() const { return image.isNull(); }
  bool isUpToDate(uint64_t current_ns) const { return last_processed_ns_ == current_ns; }
  void setHighlight(bool highlight);
  void clearHistory();

  QImage image;
  double min_val = 0;
  double max_val = 0;

 private:
  struct Bucket {
    double entry = 0.0, exit = 0.0;
    double min = std::numeric_limits<double>::max();
    double max = std::numeric_limits<double>::lowest();
    uint64_t min_ts = 0, max_ts = 0;

    void init(double y, uint64_t ts) {
      entry = exit = min = max = y;
      min_ts = max_ts = ts;
    }

    void update(double y, uint64_t ts) {
      exit = y;
      if (y < min) {
        min = y;
        min_ts = ts;
      }
      if (y > max) {
        max = y;
        max_ts = ts;
      }
    }
  };

  void prepareWindow(uint64_t current_ns, int time_window, const QSize& size);
  void updateDataPoints(const dbc::Signal* sig, CanEventIter first, CanEventIter last);
  void mapHistoryToPoints();
  void updateValueBounds();
  void flushBucket(int x, const Bucket& b);
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
  const dbc::Signal* signal_ = nullptr;
};
