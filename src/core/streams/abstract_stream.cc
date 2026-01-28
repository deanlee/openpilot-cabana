#include "abstract_stream.h"

#include <cstring>
#include <limits>
#include <utility>

#include <QApplication>
#include "common/timing.h"
#include "modules/settings/settings.h"

static const int EVENT_NEXT_BUFFER_SIZE = 6 * 1024 * 1024;  // 6MB

template <>
uint64_t TimeIndex<const CanEvent*>::get_timestamp(const CanEvent* const& e) {
  return e->mono_time;
}

AbstractStream::AbstractStream(QObject *parent) : QObject(parent) {
  assert(parent != nullptr);
  event_buffer_ = std::make_unique<MonotonicBuffer>(EVENT_NEXT_BUFFER_SIZE);

  connect(this, &AbstractStream::seekedTo, this, &AbstractStream::updateSnapshotsTo);
  connect(this, &AbstractStream::seeking, this, [this](double sec) { current_sec_ = sec; });
  connect(GetDBC(), &dbc::Manager::DBCFileChanged, this, &AbstractStream::updateMasks);
  connect(GetDBC(), &dbc::Manager::maskUpdated, this, &AbstractStream::updateMasks);
}

void AbstractStream::updateMasks() {
  std::lock_guard lk(mutex_);
  shared_state_.masks.clear();
  if (settings.suppress_defined_signals) {
    for (const auto s : sources) {
      for (const auto &[address, m] : GetDBC()->getMessages(s)) {
        shared_state_.masks[{(uint8_t)s, address}] = m.mask;
      }
    }
  }

  for (auto &[id, state] : shared_state_.master_state) {
    state.applyMask(shared_state_.masks.count(id) ? shared_state_.masks[id] : std::vector<uint8_t>{});
  }
}

void AbstractStream::suppressDefinedSignals(bool suppress) {
  settings.suppress_defined_signals = suppress;
  updateMasks();
}

size_t AbstractStream::suppressHighlighted() {
  std::lock_guard lk(mutex_);
  size_t cnt = 0;
  for (auto &[id, m] : shared_state_.master_state) {
    cnt += m.muteActiveBits(shared_state_.masks.count(id) ? shared_state_.masks[id] : std::vector<uint8_t>{});
  }
  return cnt;
}

void AbstractStream::clearSuppressed() {
  std::lock_guard lk(mutex_);
  for (auto &[id, m] : shared_state_.master_state) {
    m.unmuteActiveBits(shared_state_.masks.count(id) ? shared_state_.masks[id] : std::vector<uint8_t>{});
  }
}

void AbstractStream::commitSnapshots() {
  std::set<MessageId> msgs;
  bool structure_changed = false;
  size_t prev_src_count = sources.size();

  {
    std::lock_guard lk(mutex_);
    current_sec_ = shared_state_.current_sec;
    if (shared_state_.dirty_ids.empty()) return;

    for (const auto& id : shared_state_.dirty_ids) {
      auto& state = shared_state_.master_state[id];
      state.updateAllPatternColors(current_sec_);
      auto& target = snapshot_map_[id];
      if (target) {
        target->updateFrom(state);
      } else {
        target = std::make_unique<MessageSnapshot>(state);
        structure_changed = true;
        sources.insert(id.source);
      }
    }
    msgs = std::move(shared_state_.dirty_ids);
  }

  static double last_activity_update = 0;
  double now = millis_since_boot();
  if (now - last_activity_update > 1000) {
    updateActiveStates();
    last_activity_update = now;
  }

  if (time_range_ && (current_sec_ < time_range_->first || current_sec_ >= time_range_->second)) {
    seekTo(time_range_->first);
    return;
  }

  if (sources.size() != prev_src_count) {
    updateMasks();
    emit sourcesUpdated(sources);
  }
  emit snapshotsUpdated(&msgs, structure_changed);
}

void AbstractStream::setTimeRange(const std::optional<std::pair<double, double>> &range) {
  time_range_ = range;
  if (time_range_ && (current_sec_ < time_range_->first || current_sec_ >= time_range_->second)) {
    seekTo(time_range_->first);
  }
  emit timeRangeChanged(time_range_);
}

void AbstractStream::processNewMessage(const MessageId &id, double sec, const uint8_t *data, uint8_t size) {
  std::lock_guard lk(mutex_);
  shared_state_.current_sec = sec;
  auto &state = shared_state_.master_state[id];
  if (state.size != (size_t)size) {
    state.init(data, size, sec);
    state.applyMask(shared_state_.masks.count(id) ? shared_state_.masks[id] : std::vector<uint8_t>{});
  }
  state.update(id, data, size, sec, getSpeed());
  shared_state_.dirty_ids.insert(id);
}

const std::vector<const CanEvent *> &AbstractStream::events(const MessageId &id) const {
  static std::vector<const CanEvent *> empty_events;
  auto it = events_.find(id);
  return it != events_.end() ? it->second : empty_events;
}

const MessageSnapshot *AbstractStream::snapshot(const MessageId &id) const {
  static MessageSnapshot empty_data = {};
  auto it = snapshot_map_.find(id);
  return it != snapshot_map_.end() ? it->second.get() : &empty_data;
}

void AbstractStream::updateActiveStates() {
  const double now = current_sec_;

  for (auto& [id, m] : snapshot_map_) {
    // If never received or timestamp is in the future (during seek), inactive.
    if (m->ts <= 0 || m->ts > now) {
      m->is_active = false;
      continue;
    }

    const double elapsed = now - m->ts;

    // Expected gap between messages. Default to 2s if freq is 0.
    double expected_period = (m->freq > 0) ? (1.0 / m->freq) : 2.0;

    // Threshold: Allow 3.5 missed cycles.
    // Clamp between 2s (fast msgs) and 10s (slow heartbeats).
    const double threshold = std::clamp(expected_period * 3.5, 2.0, 10.0);

    m->is_active = (elapsed < threshold);
  }
}

void AbstractStream::updateSnapshotsTo(double sec) {
  current_sec_ = sec;

  bool has_erased = false;
  size_t origin_snapshot_size = snapshot_map_.size();
  const uint64_t last_ts = toMonoTime(sec);

  for (const auto& [id, ev] : events_) {
    auto[s_min, s_max] = time_index_map_[id].getBounds(ev.front()->mono_time, last_ts, ev.size());
    auto it = std::upper_bound(ev.begin() + s_min, ev.begin() + s_max, last_ts, CompareCanEvent());
    if (it == ev.begin()) {
      has_erased |= (shared_state_.master_state.erase(id) > 0);
      has_erased |= (snapshot_map_.erase(id) > 0);
      continue;
    }

    const CanEvent* prev_ev = *std::prev(it);
    auto& m = shared_state_.master_state[id];
    m.init(prev_ev->dat, prev_ev->size, toSeconds(prev_ev->mono_time));
    m.count = std::distance(ev.begin(), it);
    m.updateAllPatternColors(sec); // Important: Update colors before snapshotting

    auto& snap_ptr = snapshot_map_[id];
    if (!snap_ptr) {
      snap_ptr = std::make_unique<MessageSnapshot>(m);
    } else {
      snap_ptr->updateFrom(m);
    }
  }

  shared_state_.dirty_ids.clear();
  shared_state_.seek_finished = true;
  seek_finished_cv_.notify_one();
  emit snapshotsUpdated(nullptr, origin_snapshot_size != snapshot_map_.size() || has_erased);
}

void AbstractStream::waitForSeekFinshed() {
  std::unique_lock lock(mutex_);
  seek_finished_cv_.wait(lock, [this]() { return shared_state_.seek_finished; });
  shared_state_.seek_finished = false;
}

const CanEvent *AbstractStream::newEvent(uint64_t mono_time, const cereal::CanData::Reader &c) {
  auto dat = c.getDat();
  CanEvent *e = (CanEvent *)event_buffer_->allocate(sizeof(CanEvent) + sizeof(uint8_t) * dat.size());
  e->src = c.getSrc();
  e->address = c.getAddress();
  e->mono_time = mono_time;
  e->size = dat.size();
  memcpy(e->dat, (uint8_t *)dat.begin(), e->size);
  return e;
}


void AbstractStream::mergeEvents(const std::vector<const CanEvent*>& events) {
  if (events.empty()) return;

  // 1. Group events by ID
  static MessageEventsMap msg_events;
  msg_events.clear();
  for (auto e : events) msg_events[{e->src, e->address}].push_back(e);

  // 2. Global list update (O(1) fast-path for live streams)
  auto& all = all_events_;
  bool is_global_append = all.empty() || events.front()->mono_time >= all.back()->mono_time;
  auto g_pos = is_global_append ? all.end() : std::upper_bound(all.begin(), all.end(), events.front()->mono_time, CompareCanEvent());
  all.insert(g_pos, events.begin(), events.end());

  // 3. Per-ID list and Index update
  for (auto& [id, new_e] : msg_events) {
    auto& e = events_[id];
    bool is_append = e.empty() || new_e.front()->mono_time >= e.back()->mono_time;

    auto pos = is_append ? e.end() : std::upper_bound(e.begin(), e.end(), new_e.front()->mono_time, CompareCanEvent());
    e.insert(pos, new_e.begin(), new_e.end());

    time_index_map_[id].sync(e, e.front()->mono_time, e.back()->mono_time, !is_append);
  }
  emit eventsMerged(msg_events);
}

std::pair<CanEventIter, CanEventIter> AbstractStream::eventsInRange(const MessageId& id, std::optional<std::pair<double, double>> range) const {
  const auto& evs = events(id);
  if (evs.empty() || !range) return {evs.begin(), evs.end()};

  uint64_t t0 = toMonoTime(range->first), t1 = toMonoTime(range->second);
  uint64_t start_ts = evs.front()->mono_time;

  auto it_index = time_index_map_.find(id);
  if (it_index == time_index_map_.end()) {
    return {std::lower_bound(evs.begin(), evs.end(), t0, CompareCanEvent()),
            std::upper_bound(evs.begin(), evs.end(), t1, CompareCanEvent())};
  }

  const auto& index = it_index->second;

  // Narrowed search for start
  auto [s_min, s_max] = index.getBounds(start_ts, t0, evs.size());
  auto first = std::lower_bound(evs.begin() + s_min, evs.begin() + s_max, t0, CompareCanEvent());

  // Narrowed search for end
  auto [e_min, e_max] = index.getBounds(start_ts, t1, evs.size());
  auto last = std::upper_bound(std::max(first, evs.begin() + e_min), evs.begin() + e_max, t1, CompareCanEvent());

  return {first, last};
}
