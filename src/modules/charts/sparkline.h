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
  const T& operator[](size_t i) const {
    return buffer[(head - count + i) & (N - 1)];
  }
  const T& front() const { return (*this)[0]; }
  const T& back() const { return (*this)[count - 1]; }
  void pop_front_n(size_t n) { count = (n >= count) ? 0 : count - n; }
  void pop_front() { if (count > 0) count--; }
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
  void update(const dbc::Signal* sig, CanEventIter first, CanEventIter last, int time_range, QSize size);
  inline double freq() const { return freq_; }
  bool isEmpty() const { return image.isNull(); }
  void setHighlight(bool highlight);
  void clearHistory();

  QImage image;
  double min_val = 0;
  double max_val = 0;

 private:

  void updateDataPoints(const dbc::Signal* sig, CanEventIter first, CanEventIter last);
  void updateRenderPoints(int time_range, QSize size);
  void render();

  RingBuffer<DataPoint> history_;
  uint64_t last_processed_mono_ns_ = 0;

  std::vector<QPointF> render_points_;
  uint64_t current_window_max_ts_ = 0;
  std::vector<QPointF> render_pts_;
  double freq_ = 0;
  bool is_highlighted_ = false;
  const dbc::Signal* signal_ = nullptr;
};
