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

  connect(this, &AbstractStream::privateUpdateLastMsgsSignal, this, &AbstractStream::commitSnapshots, Qt::QueuedConnection);
  connect(this, &AbstractStream::seekedTo, this, &AbstractStream::updateSnapshotsTo);
  connect(this, &AbstractStream::seeking, this, [this](double sec) { current_sec_ = sec; });
  connect(GetDBC(), &dbc::Manager::DBCFileChanged, this, &AbstractStream::updateMasks);
  connect(GetDBC(), &dbc::Manager::maskUpdated, this, &AbstractStream::updateMasks);
}

void AbstractStream::updateMasks() {
  std::lock_guard lk(mutex_);
  masks_.clear();
  if (settings.suppress_defined_signals) {
    for (const auto s : sources) {
      for (const auto &[address, m] : GetDBC()->getMessages(s)) {
        masks_[{(uint8_t)s, address}] = m.mask;
      }
    }
  }

  for (auto &[id, state] : master_state_) {
    updateMessageMask(id, state);
  }
}

void AbstractStream::suppressDefinedSignals(bool suppress) {
  settings.suppress_defined_signals = suppress;
  updateMasks();
}

size_t AbstractStream::suppressHighlighted() {
  std::lock_guard lk(mutex_);
  size_t cnt = 0;
  for (auto &[id, m] : master_state_) {
    bool mod = false;
    for (auto &s : m.byte_states) {
      if (!s.is_suppressed && (current_sec_ - s.last_change_ts < 2.0)) {
        s.is_suppressed = mod = true;
      }
      cnt += s.is_suppressed;
    }
    if (mod) updateMessageMask(id, m);
  }
  return cnt;
}

void AbstractStream::clearSuppressed() {
  std::lock_guard lk(mutex_);
  for (auto &[id, m] : master_state_) {
    for (auto &state : m.byte_states) {
      state.is_suppressed = false;
    }
    // Refresh the mask (this will re-allow highlights for these bits)
    updateMessageMask(id, m);
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
    data.updateAllPatternColors(current_sec_);
    auto& target = snapshot_map_[id];
    if (target) {
      *target = std::move(data);
    } else {
      target = std::make_unique<MessageState>(std::move(data));
      structure_changed = true;
    }

    if (sources.insert(id.source).second) structure_changed = true;
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
  auto &state = master_state_[id];
  if (state.dat.size() != (size_t)size) {
    state.init(data, size, sec);
    updateMessageMask(id, state);
  }
  state.update(id, data, size, sec, getSpeed());
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

void AbstractStream::updateActiveStates() {
  double now = current_sec_;
  const double fps_margin = 1.0 / std::max(1, settings.fps);
  for (auto& [id, m] : snapshot_map_) { // Assuming a map or container of MessageStates
    if (m->ts > 0) {
      double elapsed = now - m->ts;
      double threshold = (m->freq < 0.1) ? 1.5 : (5.0 / m->freq) + fps_margin;
      m->is_active = (elapsed >= 0 && elapsed < threshold);
    } else {
      m->is_active = false;
    }
  }
}

void AbstractStream::updateSnapshotsTo(double sec) {
  current_sec_ = sec;
  uint64_t last_ts = toMonoTime(sec);
  std::unordered_map<MessageId, MessageState> next_state;
  next_state.reserve(events_.size());

  bool id_changed = false;

  for (const auto& [id, ev] : events_) {
    auto it_idx = time_index_map_.find(id);
    size_t s_min = 0, s_max = ev.size();
    if (it_idx != time_index_map_.end()) {
      std::tie(s_min, s_max) = it_idx->second.getBounds(ev.front()->mono_time, last_ts, ev.size());
    }

    auto it = std::upper_bound(ev.begin() + s_min, ev.begin() + s_max, last_ts, CompareCanEvent());

    if (it == ev.begin()) continue;

    auto& m = next_state[id];
    const CanEvent* prev_ev = *std::prev(it);

    // Carry over suppression, frequency, and mask state from previous snapshot for this message ID.
    // This ensures byte suppression and mask settings persist across timeline seeks.
    if (auto old_it = master_state_.find(id); old_it != master_state_.end()) {
      m.freq = old_it->second.freq;
      m.ignore_bit_mask = old_it->second.ignore_bit_mask;
      const auto& old_bytes = old_it->second.byte_states;
      m.byte_states.resize(old_bytes.size());
      for (size_t i = 0; i < old_bytes.size(); ++i) {
        m.byte_states[i].is_suppressed = old_bytes[i].is_suppressed;
      }
    }

    m.update(id, prev_ev->dat, prev_ev->size, toSeconds(prev_ev->mono_time), getSpeed());
    m.count = std::distance(ev.begin(), it);

    auto& snap_ptr = snapshot_map_[id];
    if (!snap_ptr) {
      snap_ptr = std::make_unique<MessageState>(m);
      id_changed = true;
    } else {
      *snap_ptr = m;
    }
  }

  // Handle ID lifecycle (newly appeared or disappeared messages)
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

void AbstractStream::updateMessageMask(const MessageId& id, MessageState& state) {
  state.ignore_bit_mask.fill(0);
  if (state.dat.empty()) return;

  const auto& dbc_mask = masks_[id];
  for (size_t i = 0; i < state.dat.size(); ++i) {
    uint8_t m = 0;
    if (state.byte_states[i].is_suppressed) {
      m = 0xFF;
    } else if (i < dbc_mask.size()) {
      m = dbc_mask[i];
    }

    state.ignore_bit_mask[i / 8] |= (static_cast<uint64_t>(m) << ((i % 8) * 8));

    if (m == 0xFF) {
      state.bit_flips[i].fill(0);
      state.bit_high_counts[i].fill(0);
    }
  }
}
