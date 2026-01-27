#pragma once

#include <QDateTime>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cereal/messaging/messaging.h"
#include "core/dbc/dbc_manager.h"
#include "message_state.h"
#include "replay/include/util.h"
#include "replay/include/replay.h"
#include "utils/time_index.h"
#include "utils/util.h"

struct CanEvent {
  uint8_t src;
  uint32_t address;
  uint64_t mono_time;
  uint8_t size;
  uint8_t dat[];
};

struct CompareCanEvent {
  constexpr bool operator()(const CanEvent* e, uint64_t ts) const noexcept {
    return e->mono_time < ts;
  }
  constexpr bool operator()(uint64_t ts, const CanEvent* e) const noexcept {
    return ts < e->mono_time;
  }
};

class MessageSnapshot {
 public:
  MessageSnapshot() = default;
  MessageSnapshot(const MessageState& s)
      : ts(s.ts), dat(s.dat), colors(s.colors), freq(s.freq), count(s.count), bit_flips(s.bit_flips), is_active(true) {}

  double ts = 0.0;
  double freq = 0.0;
  uint32_t count = 0;
  bool is_active = false;
  std::vector<uint8_t> dat;
  std::vector<uint32_t> colors;
  std::vector<std::array<uint32_t, 8>> bit_flips;
};

using MessageEventsMap = std::unordered_map<MessageId, std::vector<const CanEvent *>>;
using CanEventIter = std::vector<const CanEvent *>::const_iterator;

class AbstractStream : public QObject {
  Q_OBJECT

public:
  AbstractStream(QObject *parent);
  virtual ~AbstractStream() {}
  virtual void start() = 0;
  virtual bool liveStreaming() const { return true; }
  virtual void seekTo(double ts) {}
  virtual QString routeName() const = 0;
  virtual QString carFingerprint() const { return ""; }
  virtual QDateTime beginDateTime() const { return {}; }
  virtual uint64_t beginMonoTime() const { return 0; }
  virtual double minSeconds() const { return 0; }
  virtual double maxSeconds() const { return 0; }
  virtual void setSpeed(float speed) {}
  virtual double getSpeed() { return 1; }
  virtual bool isPaused() const { return false; }
  virtual void pause(bool pause) {}
  void setTimeRange(const std::optional<std::pair<double, double>> &range);
  const std::optional<std::pair<double, double>> &timeRange() const { return time_range_; }

  inline double currentSec() const { return current_sec_; }
  inline uint64_t toMonoTime(double sec) const { return beginMonoTime() + std::max(sec, 0.0) * 1e9; }
  inline double toSeconds(uint64_t mono_time) const { return std::max(0.0, (mono_time - beginMonoTime()) / 1e9); }

  inline const std::unordered_map<MessageId, std::unique_ptr<MessageSnapshot>> &snapshots() const { return snapshot_map_; }
  inline const MessageEventsMap &eventsMap() const { return events_; }
  inline const std::vector<const CanEvent *> &allEvents() const { return all_events_; }
  const MessageSnapshot* snapshot(const MessageId& id) const;
  const std::vector<const CanEvent *> &events(const MessageId &id) const;
  std::pair<CanEventIter, CanEventIter> eventsInRange(const MessageId &id, std::optional<std::pair<double, double>> time_range) const;

  size_t suppressHighlighted();
  void clearSuppressed();
  void suppressDefinedSignals(bool suppress);

signals:
  void paused();
  void resume();
  void seeking(double sec);
  void seekedTo(double sec);
  void timeRangeChanged(const std::optional<std::pair<double, double>> &range);
  void eventsMerged(const MessageEventsMap &events_map);
  void snapshotsUpdated(const std::set<MessageId> *ids, bool needs_rebuild);
  void sourcesUpdated(const SourceSet &s);
  void qLogLoaded(std::shared_ptr<LogReader> qlog);

public:
  SourceSet sources;

protected:
  void commitSnapshots();
  void mergeEvents(const std::vector<const CanEvent *> &events);
  const CanEvent *newEvent(uint64_t mono_time, const cereal::CanData::Reader &c);
  void processNewMessage(const MessageId &id, double sec, const uint8_t *data, uint8_t size);
  void waitForSeekFinshed();
  std::vector<const CanEvent *> all_events_;
  double current_sec_ = 0;
  std::optional<std::pair<double, double>> time_range_;

private:
  void updateSnapshotsTo(double sec);
  void updateMasks();
  void updateMessageMask(const MessageId& id, MessageState& state);
  void updateActiveStates();

  MessageEventsMap events_;
  std::unordered_map<MessageId, std::unique_ptr<MessageSnapshot>> snapshot_map_;
  std::unique_ptr<MonotonicBuffer> event_buffer_;

  // Members accessed in multiple threads. (mutex protected)
  std::mutex mutex_;
  std::condition_variable seek_finished_cv_;
  bool seek_finished_ = false;
  std::set<MessageId> dirty_ids_;
  std::unordered_map<MessageId, MessageState> master_state_;
  std::unordered_map<MessageId, TimeIndex<const CanEvent*>> time_index_map_;
  std::unordered_map<MessageId, std::vector<uint8_t>> masks_;
};

class DummyStream : public AbstractStream {
  Q_OBJECT
public:
  DummyStream(QObject *parent) : AbstractStream(parent) {}
  QString routeName() const override { return tr("No Stream"); }
  void start() override {}
};
