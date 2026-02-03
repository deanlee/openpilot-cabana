#include "chart_signal.h"

#include "modules/system/stream_manager.h"

static void appendCanEvents(const dbc::Signal* sig, const std::vector<const CanEvent*>& events,
                            std::vector<QPointF>& vals, std::vector<QPointF>& step_vals,
                            SeriesBounds &series_bounds) {
  vals.reserve(vals.size() + events.capacity());
  step_vals.reserve(step_vals.size() + events.capacity() * 2);

  double value = 0;
  auto* can = StreamManager::stream();
  for (const CanEvent* e : events) {
    if (sig->parse(e->dat, e->size, &value)) {
      const double ts = can->toSeconds(e->mono_ns);
      vals.emplace_back(ts, value);

      series_bounds.addPoint(value);

      if (!step_vals.empty())
        step_vals.emplace_back(ts, step_vals.back().y());
      step_vals.emplace_back(ts, value);
    }
  }
}

void ChartSignal::prepareData(const MessageEventsMap* msg_new_events, double min_x, double max_x) {
  // If no new events provided, we are doing a full refresh/clear
  if (!msg_new_events) {
    vals.clear();
    step_vals.clear();
    series_bounds.clear();
  }

  auto* can = StreamManager::stream();
  auto events = msg_new_events ? msg_new_events : &can->eventsMap();
  auto it = events->find(msg_id);
  if (it == events->end() || it->second.empty()) return;

  if (vals.empty() || can->toSeconds(it->second.back()->mono_ns) > vals.back().x()) {
    appendCanEvents(sig, it->second, vals, step_vals, series_bounds);
  } else {
    std::vector<QPointF> tmp_vals, tmp_step_vals;
    appendCanEvents(sig, it->second, tmp_vals, tmp_step_vals, series_bounds);
    vals.insert(std::lower_bound(vals.begin(), vals.end(), tmp_vals.front().x(), xLessThan),
                tmp_vals.begin(), tmp_vals.end());
    step_vals.insert(std::lower_bound(step_vals.begin(), step_vals.end(), tmp_step_vals.front().x(), xLessThan),
                     tmp_step_vals.begin(), tmp_step_vals.end());

    // Rebuild the bounds cache to ensure hierarchy is correct after insertion
    series_bounds.clear();
    for (const auto& p : vals) series_bounds.addPoint(p.y());
  }

  last_range_ = {-1.0, -1.0};
  updateRange(min_x, max_x);
}

void ChartSignal::updateSeries(SeriesType series_type) {
  const auto& points = series_type == SeriesType::StepLine ? step_vals : vals;
  series->replace(QVector<QPointF>(points.cbegin(), points.cend()));
}

void ChartSignal::updateRange(double min_x, double max_x) {
  if (min_x == last_range_.first && max_x == last_range_.second) {
    return;
  }
  last_range_ = {min_x, max_x};

  min_value = std::numeric_limits<double>::max();
  max_value = std::numeric_limits<double>::lowest();
  if (vals.empty()) {
    return;
  }

  auto first = std::lower_bound(vals.cbegin(), vals.cend(), min_x, xLessThan);
  auto last = std::lower_bound(first, vals.cend(), max_x, xLessThan);

  int l_idx = std::distance(vals.cbegin(), first);
  int r_idx = std::distance(vals.cbegin(), last) - 1;

  if (l_idx <= r_idx) {
    // Hierarchical query is O(log N) for both Live and Log data
    auto node = series_bounds.query(l_idx, r_idx, vals);
    min_value = node.min;
    max_value = node.max;
  }
}

std::tuple<double, double, int> getNiceAxisNumbers(qreal min, qreal max, int tick_count) {
  qreal range = niceNumber((max - min), true);  // range with ceiling
  qreal step = niceNumber(range / (tick_count - 1), false);
  min = std::floor(min / step);
  max = std::ceil(max / step);
  tick_count = int(max - min) + 1;
  return {min * step, max * step, tick_count};
}

// nice numbers can be expressed as form of 1*10^n, 2* 10^n or 5*10^n
qreal niceNumber(qreal x, bool ceiling) {
  qreal z = std::pow(10, std::floor(std::log10(x))); //find corresponding number of the form of 10^n than is smaller than x
  qreal q = x / z; //q<10 && q>=1;
  if (ceiling) {
    if (q <= 1.0) q = 1;
    else if (q <= 2.0) q = 2;
    else if (q <= 5.0) q = 5;
    else q = 10;
  } else {
    if (q < 1.5) q = 1;
    else if (q < 3.0) q = 2;
    else if (q < 7.0) q = 5;
    else q = 10;
  }
  return q * z;
}
