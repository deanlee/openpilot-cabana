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

  QObject::connect(this, &AbstractStream::privateUpdateLastMsgsSignal, this, &AbstractStream::commitSnapshots, Qt::QueuedConnection);
  QObject::connect(this, &AbstractStream::seekedTo, this, &AbstractStream::updateSnapshotsTo);
  QObject::connect(this, &AbstractStream::seeking, this, [this](double sec) { current_sec_ = sec; });
  QObject::connect(dbc(), &DBCManager::DBCFileChanged, this, &AbstractStream::updateMasks);
  QObject::connect(dbc(), &DBCManager::maskUpdated, this, &AbstractStream::updateMasks);
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
    const int size = std::min(mask.size(), m.last_changes.size());
    for (int i = 0; i < size; ++i) {
      for (int j = 0; j < 8; ++j) {
        if (((mask[i] >> (7 - j)) & 1) != 0) m.bit_flip_counts[i][j] = 0;
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
    for (auto &last_change : m.last_changes) {
      const double dt = current_sec_ - last_change.ts;
      if (dt < 2.0) {
        last_change.suppressed = true;
      }
      cnt += last_change.suppressed;
    }
    for (auto &flip_counts : m.bit_flip_counts) flip_counts.fill(0);
  }
  return cnt;
}

void AbstractStream::clearSuppressed() {
  std::lock_guard lk(mutex_);
  for (auto &[_, m] : master_state_) {
    std::for_each(m.last_changes.begin(), m.last_changes.end(), [](auto &c) { c.suppressed = false; });
  }
}

void AbstractStream::commitSnapshots() {
  std::vector<std::pair<MessageId, CanData>> snapshots;
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
      target = std::make_unique<CanData>(std::move(data));
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

void AbstractStream::updateEvent(const MessageId &id, double sec, const uint8_t *data, uint8_t size) {
  std::lock_guard lk(mutex_);
  master_state_[id].update(id, data, size, sec, getSpeed(), masks_[id]);
  dirty_ids_.insert(id);
}

const std::vector<const CanEvent *> &AbstractStream::events(const MessageId &id) const {
  static std::vector<const CanEvent *> empty_events;
  auto it = events_.find(id);
  return it != events_.end() ? it->second : empty_events;
}

const CanData *AbstractStream::snapshot(const MessageId &id) const {
  static CanData empty_data = {};
  auto it = snapshot_map_.find(id);
  return it != snapshot_map_.end() ? it->second.get() : &empty_data;
}

bool AbstractStream::isMessageActive(const MessageId &id) const {
  if (id.source == INVALID_SOURCE) {
    return false;
  }
  // Check if the message is active based on time difference and frequency
  const auto *m = snapshot(id);
  float delta = currentSec() - m->ts;

  if (m->freq < std::numeric_limits<double>::epsilon()) {
    return delta < 1.5;
  }

  return delta < (5.0 / m->freq) + (1.0 / settings.fps);
}

void AbstractStream::updateSnapshotsTo(double sec) {
  current_sec_ = sec;
  uint64_t last_ts = toMonoTime(sec);
  std::unordered_map<MessageId, CanData> next_state;
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
      const auto& old_changes = old_it->second.last_changes;
      m.last_changes.resize(old_changes.size());
      for (size_t i = 0; i < old_changes.size(); ++i) {
        m.last_changes[i].suppressed = old_changes[i].suppressed;
      }
    }
    m.update(id, prev_ev->dat, prev_ev->size, toSeconds(prev_ev->mono_time), getSpeed(), {}, freq);
    m.count = std::distance(ev.begin(), it);

    auto& snap_ptr = snapshot_map_[id];
    if (!snap_ptr) {
      snap_ptr = std::make_unique<CanData>(m);
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

void AbstractStream::mergeEvents(const std::vector<const CanEvent *> &events) {
  static MessageEventsMap msg_events;
  std::for_each(msg_events.begin(), msg_events.end(), [](auto &e) { e.second.clear(); });

  // Group events by message ID
  for (auto e : events) {
    msg_events[{e->src, e->address}].push_back(e);
  }

  if (!events.empty()) {
    for (const auto &[id, new_e] : msg_events) {
      if (!new_e.empty()) {
        auto &e = events_[id];
        auto pos = std::upper_bound(e.cbegin(), e.cend(), new_e.front()->mono_time, CompareCanEvent());
        e.insert(pos, new_e.cbegin(), new_e.cend());
      }
    }
    auto pos = std::upper_bound(all_events_.cbegin(), all_events_.cend(), events.front()->mono_time, CompareCanEvent());
    all_events_.insert(pos, events.cbegin(), events.cend());
    emit eventsMerged(msg_events);
  }
}

std::pair<CanEventIter, CanEventIter> AbstractStream::eventsInRange(const MessageId &id, std::optional<std::pair<double, double>> time_range) const {
  const auto &events = can->events(id);
  if (!time_range) return {events.begin(), events.end()};

  auto first = std::lower_bound(events.begin(), events.end(), can->toMonoTime(time_range->first), CompareCanEvent());
  auto last = std::upper_bound(first, events.end(), can->toMonoTime(time_range->second), CompareCanEvent());
  return {first, last};
}
