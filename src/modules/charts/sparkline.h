#pragma once

#include <QPixmap>
#include <QPointF>
#include <deque>
#include <vector>

#include "core/dbc/dbc_message.h"
#include "core/streams/abstractstream.h"

class Sparkline {
 public:
  struct DataPoint {
    uint64_t mono_time;
    double value;
  };
  void update(const dbc::Signal* sig, CanEventIter first, CanEventIter last, int time_range, QSize size);
  inline double freq() const { return freq_; }
  bool isEmpty() const { return pixmap.isNull(); }
  void setHighlight(bool highlight);
  void clearHistory() { history_.clear(); }

  QPixmap pixmap;
  double min_val = 0;
  double max_val = 0;

 private:
  struct Column {
    double min_val = std::numeric_limits<double>::max();
    double max_val = std::numeric_limits<double>::lowest();
    bool has_data = false;
  };

  void updateDataPoints(const dbc::Signal* sig, CanEventIter first, CanEventIter last);
  void updateRenderPoints(int time_range, QSize size);
  void render();

  std::vector<QPointF> render_points_;
  uint64_t last_processed_mono_time_ = 0;
  std::deque<DataPoint> history_;
  uint64_t current_window_min_ts_ = 0;
  uint64_t current_window_max_ts_ = 0;
  std::vector<QPointF> render_pts_;
  std::vector<Column> cols_;
  double freq_ = 0;
  bool is_highlighted_ = false;
  const dbc::Signal* signal_ = nullptr;
};
