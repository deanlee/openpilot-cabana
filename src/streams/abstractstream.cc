#include "streams/abstractstream.h"

#include <limits>
#include <utility>

#include <QApplication>
#include "common/timing.h"
#include "settings.h"

static const int EVENT_NEXT_BUFFER_SIZE = 6 * 1024 * 1024;  // 6MB

AbstractStream *can = nullptr;

AbstractStream::AbstractStream(QObject *parent) : QObject(parent) {
  assert(parent != nullptr);
  event_buffer_ = std::make_unique<MonotonicBuffer>(EVENT_NEXT_BUFFER_SIZE);

  connect(this, &AbstractStream::privateUpdateLastMsgsSignal, this, &AbstractStream::commitSnapshots, Qt::QueuedConnection);
  connect(this, &AbstractStream::seekedTo, this, &AbstractStream::updateSnapshotsTo);
  connect(this, &AbstractStream::seeking, this, [this](double sec) { current_sec_ = sec; });
  connect(dbc(), &DBCManager::DBCFileChanged, this, &AbstractStream::updateMasks);
  connect(dbc(), &DBCManager::maskUpdated, this, &AbstractStream::updateMasks);
}

void AbstractStream::updateMasks() {
  std::lock_guard lk(mutex_);
  masks_.clear();
  if (!settings.suppress_defined_signals)
    return;

  for (const auto s : sources) {
    for (const auto &[address, m] : dbc()->getMessages(s)) {
      masks_[{(uint8_t)s, address}] = m.mask;
    }
  }
  // clear bit change counts
  for (auto &[id, m] : master_state_) {
    auto &mask = masks_[id];
    const int size = std::min(mask.size(), m.byte_states.size());
    for (int i = 0; i < size; ++i) {
      for (int j = 0; j < 8; ++j) {
        if (((mask[i] >> (7 - j)) & 1) != 0) m.bit_flips[i][j] = 0;
      }
    }
  }
}

void AbstractStream::suppressDefinedSignals(bool suppress) {
  settings.suppress_defined_signals = suppress;
  updateMasks();
}

size_t AbstractStream::suppressHighlighted() {
  std::lock_guard lk(mutex_);
  size_t cnt = 0;
  for (auto &[_, m] : master_state_) {
    for (auto &state : m.byte_states) {
      const double dt = current_sec_ - state.last_ts;
      if (dt < 2.0) {
        state.suppressed = true;
      }
      cnt += state.suppressed;
    }
    for (auto &flip_counts : m.bit_flips) flip_counts.fill(0);
  }
  return cnt;
}

void AbstractStream::clearSuppressed() {
  std::lock_guard lk(mutex_);
  for (auto &[_, m] : master_state_) {
    std::for_each(m.byte_states.begin(), m.byte_states.end(), [](auto &c) { c.suppressed = false; });
  }
}

void AbstractStream::commitSnapshots() {
  std::vector<std::pair<MessageId, MessageState>> snapshots;
  std::set<MessageId> msgs;

  {
    std::lock_guard lk(mutex_);
    if (dirty_ids_.empty()) return;

    snapshots.reserve(dirty_ids_.size());
    for (const auto& id : dirty_ids_) {
      snapshots.emplace_back(id, master_state_[id]);
    }
    msgs = std::move(dirty_ids_);
  }

  bool structure_changed = false;
  const size_t prev_src_count = sources.size();

  for (auto& [id, data] : snapshots) {
    current_sec_ = std::max(current_sec_, data.ts);

    auto& target = snapshot_map_[id];
    if (target) {
      *target = std::move(data);
    } else {
      target = std::make_unique<MessageState>(std::move(data));
      structure_changed = true;
    }

    if (sources.insert(id.source).second) structure_changed = true;
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
  master_state_[id].update(id, data, size, sec, getSpeed(), masks_[id]);
  dirty_ids_.insert(id);
}

const std::vector<const CanEvent *> &AbstractStream::events(const MessageId &id) const {
  static std::vector<const CanEvent *> empty_events;
  auto it = events_.find(id);
  return it != events_.end() ? it->second : empty_events;
}

const MessageState *AbstractStream::snapshot(const MessageId &id) const {
  static MessageState empty_data = {};
  auto it = snapshot_map_.find(id);
  return it != snapshot_map_.end() ? it->second.get() : &empty_data;
}

bool AbstractStream::isMessageActive(const MessageId& id) const {
  const auto* m = snapshot(id);
  if (!m || m->ts <= 0) return false;

  double elapsed = current_sec_ - m->ts;
  if (elapsed < 0) return true;  // Handling seek/jitter

  // If freq is low/zero, 1.5s timeout.
  // If freq is high, wait for 5 missed packets + 1 UI frame margin.
  double threshold = (m->freq < 0.1) ? 1.5 : (5.0 / m->freq) + (1.0 / settings.fps);
  return elapsed < threshold;
}

void AbstractStream::updateSnapshotsTo(double sec) {
  current_sec_ = sec;
  uint64_t last_ts = toMonoTime(sec);
  std::unordered_map<MessageId, MessageState> next_state;
  next_state.reserve(events_.size());

  bool id_changed = false;

  for (const auto& [id, ev] : events_) {
    auto it = std::upper_bound(ev.begin(), ev.end(), last_ts, CompareCanEvent());
    if (it == ev.begin()) continue;

    auto& m = next_state[id];
    auto prev_ev = *std::prev(it);
    double freq = 0;
    // Keep suppressed bits.
    if (auto old_it = master_state_.find(id); old_it != master_state_.end()) {
      freq = old_it->second.freq;
      const auto& old_changes = old_it->second.byte_states;
      m.byte_states.resize(old_changes.size());
      for (size_t i = 0; i < old_changes.size(); ++i) {
        m.byte_states[i].suppressed = old_changes[i].suppressed;
      }
    }
    m.update(id, prev_ev->dat, prev_ev->size, toSeconds(prev_ev->mono_time), getSpeed(), {}, freq);
    m.count = std::distance(ev.begin(), it);

    auto& snap_ptr = snapshot_map_[id];
    if (!snap_ptr) {
      snap_ptr = std::make_unique<MessageState>(m);
      id_changed = true;
    } else {
      *snap_ptr = m;
    }
  }

  if (!id_changed && next_state.size() != snapshot_map_.size()) {
    id_changed = true;
  }

  if (id_changed) {
    for (auto it = snapshot_map_.begin(); it != snapshot_map_.end();) {
      if (next_state.find(it->first) == next_state.end()) {
        it = snapshot_map_.erase(it);
      } else {
        ++it;
      }
    }
  }

  dirty_ids_.clear();
  master_state_ = std::move(next_state);

  emit snapshotsUpdated(nullptr, id_changed);

  std::lock_guard lk(mutex_);
  seek_finished_ = true;
  seek_finished_cv_.notify_one();
}

void AbstractStream::waitForSeekFinshed() {
  std::unique_lock lock(mutex_);
  seek_finished_cv_.wait(lock, [this]() { return seek_finished_; });
  seek_finished_ = false;
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

  // 1. Group by ID more efficiently
  // Hint: If MessageEventsMap is a map, it creates many small allocations.
  // Using a local temporary grouping is often cleaner.
  MessageEventsMap msg_events; 
  for (auto e : events) {
    msg_events[{e->src, e->address}].push_back(e);
  }

  // Determine if we are appending to the global log
  bool is_global_append = all_events_.empty() || events.front()->mono_time >= all_events_.back()->mono_time;

  for (const auto& [id, new_e] : msg_events) {
    auto& e = events_[id];
    bool is_append = e.empty() || new_e.front()->mono_time >= e.back()->mono_time;

    if (is_append) {
      e.insert(e.end(), new_e.begin(), new_e.end());
    } else {
      auto pos = std::upper_bound(e.begin(), e.end(), new_e.front()->mono_time, CompareCanEvent());
      e.insert(pos, new_e.begin(), new_e.end());
    }

    // Indexing logic
    if (e.size() > 1000) {
      if (is_append) {
        updateIncrementalIndex(id, new_e);
      } else {
        buildTimeIndex(id);
      }
    }
  }

  // 2. Global Event List Update
  // If we are just appending data (live stream), don't use upper_bound!
  if (is_global_append) {
    all_events_.insert(all_events_.end(), events.begin(), events.end());
  } else {
    auto pos = std::upper_bound(all_events_.begin(), all_events_.end(), events.front()->mono_time, CompareCanEvent());
    all_events_.insert(pos, events.begin(), events.end());
  }

  emit eventsMerged(msg_events);
}

std::pair<size_t, size_t> AbstractStream::getBounds(const MessageId& id, uint64_t ts_ns) const {
  const auto& evs = events_.at(id);
  auto it = time_index_map_.find(id);
  if (it == time_index_map_.end() || ts_ns <= evs.front()->mono_time) {
    return {0, evs.size()};
  }

  const auto& idx_list = it->second;
  size_t sec = (ts_ns - evs.front()->mono_time) / 1000000000;

  if (sec >= idx_list.size()) return {idx_list.back(), evs.size()};

  size_t min_idx = idx_list[sec];
  size_t max_idx = (sec + 1 < idx_list.size()) ? idx_list[sec + 1] : evs.size();

  return {min_idx, max_idx};
}

void AbstractStream::updateIncrementalIndex(const MessageId& id, const std::vector<const CanEvent*>& new_events) {
  auto& idx_list = time_index_map_[id];
  const auto& all_evs = events_[id];

  if (idx_list.empty()) {
    buildTimeIndex(id);
    return;
  }

  uint64_t log_start_ns = all_evs.front()->mono_time;
  // Process only the newly appended events
  for (size_t i = all_evs.size() - new_events.size(); i < all_evs.size(); ++i) {
    size_t sec = (all_evs[i]->mono_time - log_start_ns) / 1000000000;

    // Fill all seconds between the last indexed event and this one
    // This is crucial for 0.33Hz messages to ensure no "holes" in the index
    while (idx_list.size() <= sec) {
      idx_list.push_back(i);
    }
  }
}

void AbstractStream::buildTimeIndex(const MessageId& id) {
  const auto& evs = events_[id];
  if (evs.empty()) return;

  auto& idx_list = time_index_map_[id];
  idx_list.clear();

  uint64_t log_start_ns = evs.front()->mono_time;
  for (size_t i = 0; i < evs.size(); ++i) {
    size_t sec = (evs[i]->mono_time - log_start_ns) / 1000000000;
    while (idx_list.size() <= sec) {
      idx_list.push_back(i);
    }
  }
}

std::pair<CanEventIter, CanEventIter> AbstractStream::eventsInRange(const MessageId& id, std::optional<std::pair<double, double>> range) const {
  const auto& evs = events(id);
  if (evs.empty() || !range) return {evs.begin(), evs.end()};

  uint64_t t_start = toMonoTime(range->first);
  uint64_t t_end = toMonoTime(range->second);

  // Use a simple helper that returns [min_idx, max_idx]
  auto [s_min, s_max] = getBounds(id, t_start);
  auto first = std::lower_bound(evs.begin() + s_min, evs.begin() + s_max, t_start, CompareCanEvent());

  auto [e_min, e_max] = getBounds(id, t_end);
  // Optimization: ensure 'last' search starts at 'first'
  size_t start_for_last = std::max((size_t)std::distance(evs.begin(), first), e_min);
  auto last = std::upper_bound(evs.begin() + start_for_last, evs.begin() + e_max, t_end, CompareCanEvent());

  return {first, last};
}