#pragma once

#include <QDateTime>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
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
  uint64_t mono_ns;
  uint8_t size;
  uint8_t dat[];
};

struct CompareCanEvent {
  constexpr bool operator()(const CanEvent* e, uint64_t ts) const noexcept {
    return e->mono_ns < ts;
  }
  constexpr bool operator()(uint64_t ts, const CanEvent* e) const noexcept {
    return ts < e->mono_ns;
  }
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
  virtual uint64_t beginMonoNs() const { return 0; }
  virtual double minSeconds() const { return 0; }
  virtual double maxSeconds() const { return 0; }
  virtual void setSpeed(float speed) {}
  virtual double getSpeed() { return 1; }
  virtual bool isPaused() const { return false; }
  virtual void pause(bool pause) {}
  void setTimeRange(const std::optional<std::pair<double, double>> &range);
  const std::optional<std::pair<double, double>> &timeRange() const { return time_range_; }

  inline double currentSec() const { return current_sec_; }
  inline uint64_t toMonoNs(double sec) const { return beginMonoNs() + std::max(sec, 0.0) * 1e9; }
  inline double toSeconds(uint64_t mono_ns) const {
    const uint64_t begin_ns = beginMonoNs();
    return mono_ns > begin_ns ? (mono_ns - begin_ns) / 1e9 : 0.0;
  }

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
  const CanEvent *newEvent(uint64_t mono_ns, const cereal::CanData::Reader &c);
  void processNewMessage(const MessageId &id, uint64_t mono_ns, const uint8_t *data, uint8_t size);
  void waitForSeekFinshed();

  struct SharedState {
    double current_sec = 0;
    std::set<MessageId> dirty_ids;
    std::unordered_map<MessageId, MessageState> master_state;
    std::unordered_map<MessageId, std::vector<uint8_t>> masks;
    bool mute_defined_signals = false;
    bool seek_finished = false;
  };

  std::vector<const CanEvent *> all_events_;
  double current_sec_ = 0;
  std::optional<std::pair<double, double>> time_range_;

private:
  void updateSnapshotsTo(double sec);
  void updateMasks();
  void updateActiveStates();
  void updateMessageMask(const MessageId &id);
  void applyCurrentPolicy(MessageState& state, const MessageId& id);


  std::unordered_map<MessageId, std::unique_ptr<MessageSnapshot>> snapshot_map_;

  MessageEventsMap events_;
  std::unique_ptr<MonotonicBuffer> event_buffer_;
  std::unordered_map<MessageId, TimeIndex<const CanEvent*>> time_index_map_;

  std::mutex mutex_;
  SharedState shared_state_;
  std::condition_variable seek_finished_cv_;
};

class DummyStream : public AbstractStream {
  Q_OBJECT
public:
  DummyStream(QObject *parent) : AbstractStream(parent) {}
  QString routeName() const override { return tr("No Stream"); }
  void start() override {}
};
