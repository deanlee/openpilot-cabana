#pragma once

#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QValueAxis>

#include "core/dbc/dbc_manager.h"
#include "core/streams/abstract_stream.h"
#include "utils/segment_tree.h"

using namespace QtCharts;
// Define a small value of epsilon to compare double values
const float EPSILON = 0.000001;

enum class SeriesType {
  Line = 0,
  StepLine,
  Scatter
};

class ChartSignal {
public:
  ChartSignal(const MessageId &id, const dbc::Signal *s, QXYSeries *ser)
      : msg_id(id), sig(s), series(ser) {}
  MessageId msg_id;
  const dbc::Signal* sig = nullptr;
  QXYSeries* series = nullptr;
  std::vector<QPointF> vals;
  std::vector<QPointF> step_vals;
  QPointF track_pt{};
  SegmentTree segment_tree;
  double min = 0;
  double max = 0;
  void updateSeries(SeriesType series_type, const MessageEventsMap* msg_new_events = nullptr);
  void updateRange(double main_x, double max_x);
};

inline bool xLessThan(const QPointF &p, float x) { return p.x() < (x - EPSILON); }
qreal niceNumber(qreal x, bool ceiling);
std::tuple<double, double, int> getNiceAxisNumbers(qreal min, qreal max, int tick_count);
