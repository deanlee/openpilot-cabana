#include "abstract_stream.h"

#include <QApplication>
#include <cstring>
#include <limits>
#include <utility>

#include "common/timing.h"
#include "modules/settings/settings.h"

static constexpr int EVENT_NEXT_BUFFER_SIZE = 6 * 1024 * 1024;  // 6MB

template <>
uint64_t TimeIndex<const CanEvent*>::get_timestamp(const CanEvent* const& e) {
  return e->mono_ns;
}

AbstractStream::AbstractStream(QObject* parent) : QObject(parent) {
  assert(parent != nullptr);
  event_buffer_ = std::make_unique<MonotonicBuffer>(EVENT_NEXT_BUFFER_SIZE);
  snapshot_map_.reserve(1024);
  time_index_map_.reserve(1024);
  shared_state_.master_state.reserve(1024);

  connect(this, &AbstractStream::seekedTo, this, &AbstractStream::updateSnapshotsTo);
  connect(this, &AbstractStream::seeking, this, [this](double sec) { current_sec_ = sec; });
  connect(GetDBC(), &dbc::Manager::DBCFileChanged, this, &AbstractStream::updateMasks);
  connect(GetDBC(), &dbc::Manager::maskUpdated, this, &AbstractStream::updateMessageMask);
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
      state.dirty = false;
    }
    msgs = std::move(shared_state_.dirty_ids);
  }

  updateActiveStates();

  if (time_range_ && (current_sec_ < time_range_->first || current_sec_ >= time_range_->second)) {
    seekTo(time_range_->first);
    return;
  }

  if (sources.size() != prev_src_count) {
    emit sourcesUpdated(sources);
  }
  emit snapshotsUpdated(&msgs, structure_changed);
}

void AbstractStream::setTimeRange(const std::optional<std::pair<double, double>>& range) {
  time_range_ = range;
  if (time_range_ && (current_sec_ < time_range_->first || current_sec_ >= time_range_->second)) {
    seekTo(time_range_->first);
  }
  emit timeRangeChanged(time_range_);
}

void AbstractStream::processNewMessage(const MessageId& id, uint64_t mono_ns, const uint8_t* data, uint8_t size) {
  std::lock_guard lk(mutex_);
  const double sec = toSeconds(mono_ns);
  shared_state_.current_sec = sec;

  auto& state = shared_state_.master_state[id];
  if (state.size != size) {
    state.init(data, size, sec);
    applyCurrentPolicy(state, id);
  }

  if (!state.dirty) {
    state.dirty = true;
    shared_state_.dirty_ids.insert(id);
  }
  state.update(data, size, sec);
}

const std::vector<const CanEvent*>& AbstractStream::events(const MessageId& id) const {
  static std::vector<const CanEvent*> empty_events;
  auto it = events_.find(id);
  return it != events_.end() ? it->second : empty_events;
}

const MessageSnapshot* AbstractStream::snapshot(const MessageId& id) const {
  static const MessageSnapshot kEmptySnapshot;
  auto it = snapshot_map_.find(id);
  return it != snapshot_map_.end() ? it->second.get() : &kEmptySnapshot;
}

void AbstractStream::updateSnapshotsTo(double sec) {
  current_sec_ = sec;

  bool has_erased = false;
  size_t origin_snapshot_size = snapshot_map_.size();
  const uint64_t last_ts = toMonoNs(sec);

  for (const auto& [id, ev] : events_) {
    if (ev.empty()) continue;

    auto [s_min, s_max] = time_index_map_[id].getBounds(ev.front()->mono_ns, last_ts, ev.size());
    auto it = std::ranges::upper_bound(ev.begin() + s_min, ev.begin() + s_max, last_ts, {}, &CanEvent::mono_ns);
    if (it == ev.begin()) {
      has_erased |= (shared_state_.master_state.erase(id) > 0);
      has_erased |= (snapshot_map_.erase(id) > 0);
      continue;
    }

    const CanEvent* prev_ev = *std::prev(it);
    auto& m = shared_state_.master_state[id];
    m.dirty = false;
    m.init(prev_ev->dat, prev_ev->size, toSeconds(prev_ev->mono_ns));
    m.count = std::distance(ev.begin(), it);
    m.updateAllPatternColors(sec);  // Important: Update colors before snapshotting

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

void AbstractStream::updateActiveStates() {
  const double now = millis_since_boot();
  if (now - last_activity_update_ms_ > kActivityCheckIntervalMs) {
    for (auto& [_, m] : snapshot_map_) {
      m->updateActiveState(current_sec_);
    }
    last_activity_update_ms_ = now;
  }
}

void AbstractStream::waitForSeekFinished() {
  std::unique_lock lock(mutex_);
  seek_finished_cv_.wait(lock, [this]() { return shared_state_.seek_finished; });
  shared_state_.seek_finished = false;
}

const CanEvent* AbstractStream::newEvent(uint64_t mono_ns, const cereal::CanData::Reader& c) {
  auto dat = c.getDat();
  CanEvent* e = (CanEvent*)event_buffer_->allocate(sizeof(CanEvent) + sizeof(uint8_t) * dat.size());
  e->src = c.getSrc();
  e->address = c.getAddress();
  e->mono_ns = mono_ns;
  e->size = dat.size();
  memcpy(e->dat, dat.begin(), e->size);
  return e;
}

void AbstractStream::mergeEvents(const std::vector<const CanEvent*>& events) {
  if (events.empty()) return;

  // 1. Group events by ID
  MessageEventsMap msg_events;
  msg_events.reserve(64);
  for (const auto* e : events) {
    msg_events[{e->src, e->address}].push_back(e);
  }

  // Helper lambda to insert events while maintaining time order
  auto insert_ordered = [](std::vector<const CanEvent*>& target, const std::vector<const CanEvent*>& new_evs) {
    bool is_append = target.empty() || new_evs.front()->mono_ns >= target.back()->mono_ns;
    target.reserve(target.size() + new_evs.size());

    auto pos =
        is_append ? target.end() : std::ranges::upper_bound(target, new_evs.front()->mono_ns, {}, &CanEvent::mono_ns);
    target.insert(pos, new_evs.begin(), new_evs.end());
    return is_append;
  };

  // 2. Global list update (O(1) fast-path for live streams)
  insert_ordered(all_events_, events);

  // 3. Per-ID list and Index update
  for (auto& [id, new_e] : msg_events) {
    auto& e = events_[id];
    bool was_append = insert_ordered(e, new_e);
    // Sync the time index (rebuild only if it wasn't a simple append)
    time_index_map_[id].sync(e, e.front()->mono_ns, e.back()->mono_ns, !was_append);
  }
  emit eventsMerged(msg_events);
}

std::pair<CanEventIter, CanEventIter> AbstractStream::eventsInRange(
    const MessageId& id, std::optional<std::pair<double, double>> range) const {
  const auto& evs = events(id);
  if (evs.empty() || !range) return {evs.begin(), evs.end()};

  const uint64_t t0 = toMonoNs(range->first);
  const uint64_t t1 = toMonoNs(range->second);

  auto it_index = time_index_map_.find(id);
  if (it_index == time_index_map_.end()) {
    return {std::ranges::lower_bound(evs, t0, {}, &CanEvent::mono_ns),
            std::ranges::upper_bound(evs, t1, {}, &CanEvent::mono_ns)};
  }

  const auto& index = it_index->second;
  const uint64_t start_ts = evs.front()->mono_ns;

  // Narrowed search for start
  auto [s_min, s_max] = index.getBounds(start_ts, t0, evs.size());
  auto first = std::ranges::lower_bound(evs.begin() + s_min, evs.begin() + s_max, t0, {}, &CanEvent::mono_ns);

  // Narrowed search for end
  auto [e_min, e_max] = index.getBounds(start_ts, t1, evs.size());
  auto search_start = std::max(first, evs.begin() + e_min);
  auto last = std::ranges::upper_bound(search_start, evs.begin() + e_max, t1, {}, &CanEvent::mono_ns);
  return {first, last};
}

void AbstractStream::updateMasks() {
  std::lock_guard lk(mutex_);

  shared_state_.masks.clear();
  auto* dbc_manager = GetDBC();

  // Rebuild the mask cache
  for (uint8_t s : sources) {
    for (const auto& [address, m] : dbc_manager->getMessages(s)) {
      shared_state_.masks[{s, address}] = m.mask;
    }
  }

  // Refresh all states based on the new cache
  for (auto& [id, state] : shared_state_.master_state) {
    applyCurrentPolicy(state, id);
  }
}

void AbstractStream::updateMessageMask(const MessageId& id) {
  auto* dbc_manager = GetDBC();
  std::lock_guard lk(mutex_);

  for (const uint8_t s : sources) {
    const MessageId target_id(s, id.address);
    if (const auto* m = dbc_manager->msg(target_id)) {
      shared_state_.masks[target_id] = m->mask;
    } else {
      shared_state_.masks.erase(target_id);
    }

    auto it = shared_state_.master_state.find(target_id);
    if (it != shared_state_.master_state.end()) {
      applyCurrentPolicy(it->second, target_id);
    }
  }
}

void AbstractStream::applyCurrentPolicy(MessageState& state, const MessageId& id) {
  if (shared_state_.mute_defined_signals) {
    auto it = shared_state_.masks.find(id);
    if (it != shared_state_.masks.end()) {
      state.applyMask(it->second);
      return;
    }
  }
  static const std::vector<uint8_t> empty_mask;
  state.applyMask(empty_mask);
}

void AbstractStream::suppressDefinedSignals(bool suppress) {
  {
    std::lock_guard lk(mutex_);
    if (shared_state_.mute_defined_signals == suppress) {
      return;
    }
    shared_state_.mute_defined_signals = suppress;
  }
  updateMasks();
}

size_t AbstractStream::suppressHighlighted() {
  std::lock_guard lk(mutex_);
  size_t cnt = 0;
  for (auto& [id, m] : shared_state_.master_state) {
    cnt += m.muteActiveBits(shared_state_.masks[id]);
  }
  return cnt;
}

void AbstractStream::clearSuppressed() {
  std::lock_guard lk(mutex_);
  for (auto& [id, m] : shared_state_.master_state) {
    m.unmuteActiveBits(shared_state_.masks[id]);
  }
}
