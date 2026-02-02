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

class SparklineContext {
 public:
  float pad = 2.0f;
  uint64_t last_processed_mono_ns = 0;
  uint64_t win_start_ns = 0;
  uint64_t win_end_ns = 0;
  bool jump_detected = false;
  float right_edge = 0.0f;

  CanEventIter first;
  CanEventIter last;

  QSize widget_size = {};
  float px_per_ns = 0;

  bool update(const MessageId &msg_id, uint64_t current_ns, int time_window, const QSize& size);
  inline float getX(uint64_t ts) const {
    if (ts >= win_end_ns) return right_edge;

    float x = right_edge - static_cast<float>(static_cast<double>(win_end_ns - ts) * px_per_ns);
    return (x < pad) ? pad : x;
  }
};

class Sparkline {
 public:
  struct DataPoint {
    uint64_t mono_ns;
    double value;
  };
  void update(const dbc::Signal* sig, const SparklineContext& params);
  inline double freq() const { return freq_; }
  bool isEmpty() const { return image.isNull(); }
  void mapHistoryToPoints(const SparklineContext& params);
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
      if (y > min) { min = y; min_ts = ts; } // Y increases downwards
      if (y < max) { max = y; max_ts = ts; }
    }
  };

  void updateDataPoints(const dbc::Signal* sig, const SparklineContext &ctx);
  bool calculateValueBounds();
  void flushBucket(int x, const Bucket& b);
  void addUniquePoint(int x, float y);
  void render();
  void mapFlatPath(const SparklineContext &ctx);
  void mapNoisyPath(const SparklineContext& ctx);

  RingBuffer<DataPoint> history_;
  std::vector<QPointF> render_pts_;
  double freq_ = 0;
  bool is_highlighted_ = false;
  const dbc::Signal* signal_ = nullptr;
};
