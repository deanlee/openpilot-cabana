#include "abstract_stream.h"

#include <QApplication>
#include <QTimer>
#include <cstring>
#include <limits>
#include <utility>

#include "common/timing.h"
#include "modules/settings/settings.h"

static constexpr int EVENT_BUFFER_CHUNK_SIZE = 6 * 1024 * 1024;  // 6MB

template <>
uint64_t TimeIndex<const CanEvent*>::get_timestamp(const CanEvent* const& e) {
  return e->mono_ns;
}

AbstractStream::AbstractStream(QObject* parent) : QObject(parent) {
  assert(parent != nullptr);
  event_buffer_ = std::make_unique<MonotonicBuffer>(EVENT_BUFFER_CHUNK_SIZE);
  snapshot_map_.reserve(1024);
  time_index_map_.reserve(1024);
  shared_state_.master_state.reserve(1024);

  connect(this, &AbstractStream::seekedTo, this, &AbstractStream::updateSnapshotsTo);
  connect(this, &AbstractStream::seeking, this, [this](double sec) { current_sec_ = sec; });
  connect(GetDBC(), &dbc::Manager::DBCFileChanged, this, &AbstractStream::updateMasks);
  connect(GetDBC(), &dbc::Manager::maskUpdated, this, &AbstractStream::updateMessageMask);
}

void AbstractStream::commitSnapshots() {
  std::set<MessageId> updated_ids;
  bool structure_changed = false;
  const size_t prev_source_count = sources_.size();

  {
    std::lock_guard lk(mutex_);
    current_sec_ = shared_state_.current_sec;
    if (shared_state_.dirty_ids.empty()) return;

    for (const auto& id : shared_state_.dirty_ids) {
      auto& state = shared_state_.master_state[id];
      structure_changed |= updateSnapshot(id, state);
      state.dirty = false;
    }
    updated_ids = std::move(shared_state_.dirty_ids);
  }

  // Compute colors outside lock — purely presentational, main-thread-only
  const bool is_dark = utils::isDarkTheme();
  for (const auto& id : updated_ids) {
    snapshot_map_[id]->computeColors(current_sec_, is_dark);
  }

  updateActivityStates();

  // Range Enforcement: Ensure we don't drift out of the set view
  if (time_range_ && (current_sec_ < time_range_->first || current_sec_ >= time_range_->second)) {
    seekTo(time_range_->first);
    return;
  }

  if (sources_.size() != prev_source_count) {
    emit sourcesUpdated(sources_);
  }
  emit snapshotsUpdated(&updated_ids, structure_changed);
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
    state.setDbcMask(getMask(id));
  } else {
    state.update(data, size, sec);
  }

  if (!state.dirty) {
    state.dirty = true;
    shared_state_.dirty_ids.insert(id);
  }
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
  std::unique_lock lk(mutex_);

  current_sec_ = sec;
  const uint64_t target_ns = toMonoNs(sec);

  SourceSet active_sources;
  bool has_erased = false;
  size_t origin_snapshot_size = snapshot_map_.size();

  for (const auto& [id, ev_list] : events_) {
    if (ev_list.empty()) continue;

    auto [s_min, s_max] = time_index_map_[id].getBounds(ev_list.front()->mono_ns, target_ns, ev_list.size());
    auto it = std::ranges::upper_bound(ev_list.begin() + s_min, ev_list.begin() + s_max, target_ns, {}, &CanEvent::mono_ns);
    if (it == ev_list.begin()) {
      has_erased |= (shared_state_.master_state.erase(id) > 0);
      has_erased |= (snapshot_map_.erase(id) > 0);
      continue;
    }

    const CanEvent* prev_ev = *std::prev(it);
    auto& m = shared_state_.master_state[id];
    m.dirty = false;
    m.init(prev_ev->dat, prev_ev->size, toSeconds(prev_ev->mono_ns));
    m.setDbcMask(getMask(id));
    m.count = std::distance(ev_list.begin(), it);

    updateSnapshot(id, m);
    snapshot_map_[id]->updateActiveState(sec);

    active_sources.insert(id.source);
  }

  bool sources_changed = (active_sources != sources_);
  if (sources_changed) {
    sources_ = std::move(active_sources);
  }

  shared_state_.dirty_ids.clear();
  shared_state_.seek_finished = true;
  lk.unlock();
  seek_finished_cv_.notify_one();

  // Compute colors outside lock — snapshot_map_ is main-thread-only
  const bool is_dark = utils::isDarkTheme();
  for (auto& [id, snap] : snapshot_map_) {
    snap->computeColors(sec, is_dark);
  }

  if (sources_changed) {
    emit sourcesUpdated(sources_);
  }
  emit snapshotsUpdated(nullptr, origin_snapshot_size != snapshot_map_.size() || has_erased);
}

void AbstractStream::updateActivityStates() {
  const double now = millis_since_boot();
  if (now - last_activity_update_ms_ <= kActivityCheckIntervalMs) return;
  last_activity_update_ms_ = now;

  for (auto& [id, snap] : snapshot_map_) {
    if (!snap->is_active) continue;  // Already inactive
    snap->updateActiveState(current_sec_);
  }
}

void AbstractStream::waitForSeekFinished() {
  std::unique_lock lock(mutex_);
  seek_finished_cv_.wait(lock, [this]() { return shared_state_.seek_finished; });
  shared_state_.seek_finished = false;
}

const CanEvent* AbstractStream::newEvent(uint64_t mono_ns, uint8_t src, uint32_t address, const uint8_t* data, uint8_t size) {
  auto* e = static_cast<CanEvent*>(event_buffer_->allocate(sizeof(CanEvent) + sizeof(uint8_t) * size));
  e->src = src;
  e->address = address;
  e->mono_ns = mono_ns;
  e->size = size;
  memcpy(e->dat, data, size);
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
  QTimer::singleShot(0, this, [this, msg_events = std::move(msg_events)]() mutable { emit eventsMerged(msg_events); });
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
  auto* dbc = GetDBC();

  // Rebuild the mask cache
  for (uint8_t s : sources_) {
    for (const auto& [address, msg] : dbc->getMessages(s)) {
      shared_state_.masks[{s, address}] = msg.getMask();
    }
  }

  // Refresh all states based on the new cache
  for (auto& [id, state] : shared_state_.master_state) {
    state.setDbcMask(getMask(id));
  }
}

void AbstractStream::updateMessageMask(const MessageId& id) {
  auto* dbc_manager = GetDBC();
  std::lock_guard lk(mutex_);

  for (const uint8_t s : sources_) {
    const MessageId target_id(s, id.address);
    if (const auto* m = dbc_manager->msg(target_id)) {
      shared_state_.masks[target_id] = m->getMask();
    } else {
      shared_state_.masks.erase(target_id);
    }

    auto it = shared_state_.master_state.find(target_id);
    if (it != shared_state_.master_state.end()) {
      it->second.setDbcMask(getMask(target_id));
    }
  }
}

const std::vector<uint8_t>& AbstractStream::getMask(const MessageId& id) const {
  static const std::vector<uint8_t> empty;
  if (shared_state_.mute_defined_signals) {
    if (auto it = shared_state_.masks.find(id); it != shared_state_.masks.end())
      return it->second;
  }
  return empty;
}

bool AbstractStream::updateSnapshot(const MessageId& id, const MessageState& state) {
  auto& snap = snapshot_map_[id];
  if (!snap) {
    snap = std::make_unique<MessageSnapshot>();
    snap->updateFrom(state);
    sources_.insert(id.source);
    return true;
  }
  snap->updateFrom(state);
  return false;
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
    cnt += m.muteActiveBits();
  }
  return cnt;
}

void AbstractStream::clearSuppressed() {
  std::lock_guard lk(mutex_);
  for (auto& [id, m] : shared_state_.master_state) {
    m.unmuteActiveBits();
  }
}
