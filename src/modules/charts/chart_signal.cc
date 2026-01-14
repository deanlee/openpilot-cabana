#include "chart_signal.h"

#include "modules/system/stream_manager.h"

static void appendCanEvents(const dbc::Signal* sig, const std::vector<const CanEvent*>& events,
                            std::vector<QPointF>& vals, std::vector<QPointF>& step_vals) {
  vals.reserve(vals.size() + events.capacity());
  step_vals.reserve(step_vals.size() + events.capacity() * 2);

  double value = 0;
  auto* can = StreamManager::stream();
  for (const CanEvent* e : events) {
    if (sig->getValue(e->dat, e->size, &value)) {
      const double ts = can->toSeconds(e->mono_time);
      vals.emplace_back(ts, value);
      if (!step_vals.empty())
        step_vals.emplace_back(ts, step_vals.back().y());
      step_vals.emplace_back(ts, value);
    }
  }
}

void ChartSignal::updateSeries(SeriesType series_type, const MessageEventsMap* msg_new_events) {
  if (!msg_new_events) {
    vals.clear();
    step_vals.clear();
  }

  auto* can = StreamManager::stream();
  auto events = msg_new_events ? msg_new_events : &can->eventsMap();
  auto it = events->find(msg_id);
  if (it == events->end() || it->second.empty()) return;

  if (vals.empty() || can->toSeconds(it->second.back()->mono_time) > vals.back().x()) {
    appendCanEvents(sig, it->second, vals, step_vals);
  } else {
    std::vector<QPointF> tmp_vals, tmp_step_vals;
    appendCanEvents(sig, it->second, tmp_vals, tmp_step_vals);
    vals.insert(std::lower_bound(vals.begin(), vals.end(), tmp_vals.front().x(), xLessThan),
                tmp_vals.begin(), tmp_vals.end());
    step_vals.insert(std::lower_bound(step_vals.begin(), step_vals.end(), tmp_step_vals.front().x(), xLessThan),
                     tmp_step_vals.begin(), tmp_step_vals.end());
  }

  if (!can->liveStreaming()) {
    segment_tree.build(vals);
  }
  const auto& points = series_type == SeriesType::StepLine ? step_vals : vals;
  series->replace(QVector<QPointF>(points.cbegin(), points.cend()));
}

void ChartSignal::updateRange(double main_x, double max_x) {
  auto first = std::lower_bound(vals.cbegin(), vals.cend(), main_x, xLessThan);
  auto last = std::lower_bound(first, vals.cend(), max_x, xLessThan);
  min = std::numeric_limits<double>::max();
  max = std::numeric_limits<double>::lowest();
  if (StreamManager::instance().isLiveStream()) {
    for (auto it = first; it != last; ++it) {
      if (it->y() < min) min = it->y();
      if (it->y() > max) max = it->y();
    }
  } else {
    std::tie(min, max) = segment_tree.minmax(std::distance(vals.cbegin(), first), std::distance(vals.cbegin(), last));
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
