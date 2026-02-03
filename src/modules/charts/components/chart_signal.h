#pragma once

#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QValueAxis>

#include "core/dbc/dbc_manager.h"
#include "core/streams/abstract_stream.h"
#include "utils/segment_tree.h"
#include "utils/series_bounds.h"

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
  double min_value = 0;
  double max_value = 0;
  void prepareData(const MessageEventsMap* msg_new_events, double min_x, double max_x);
  void updateRange(double main_x, double max_x);
  void updateSeries(SeriesType series_type);

private:
  SeriesBounds series_bounds;
  std::pair<double, double> last_range_{0, 0};
};

qreal niceNumber(qreal x, bool ceiling);
std::tuple<double, double, int> getNiceAxisNumbers(qreal min, qreal max, int tick_count);
