#pragma once

#include <QObject>
#include <QPointer>
#include <optional>

#include "core/streams/abstractstream.h"
#include "core/streams/replaystream.h"

class StreamManager : public QObject {
  Q_OBJECT

 public:
  inline static StreamManager& instance() {
    static StreamManager s;
    return s;
  }

  [[nodiscard]] inline static AbstractStream* stream() {
    return instance().stream_;
  }

  void setStream(AbstractStream* new_stream, const QString& dbc_file = {});
  void shutdown();
  void closeStream();

  [[nodiscard]] inline AbstractStream* currentStream() const { return stream_; }
  [[nodiscard]] inline bool hasStream() const { return !isDummy(); }
  [[nodiscard]] inline bool isDummy() const {
    return !stream_ || dynamic_cast<DummyStream*>(stream_) != nullptr;
  }
  [[nodiscard]] inline bool isReplayStream() const {
    return stream_ && dynamic_cast<ReplayStream*>(stream_) != nullptr;
  }
  [[nodiscard]] inline bool isLiveStream() const {
    return stream_ && stream_->liveStreaming();
  }

  // Prevent Copying
  StreamManager(const StreamManager&) = delete;
  StreamManager& operator=(const StreamManager&) = delete;

 signals:
  void streamChanged();
  void streamStarted(AbstractStream* stream, const QString& dbc_file);
  void streamError(const QString& message);

  void paused();
  void resume();
  void seeking(double sec);
  void seekedTo(double sec);

  void timeRangeChanged(const std::optional<std::pair<double, double>>& range);
  void eventsMerged(const MessageEventsMap& events_map);
  void snapshotsUpdated(const std::set<MessageId>* ids, bool needs_rebuild);
  void sourcesUpdated(const SourceSet& s);
  void qLogLoaded(std::shared_ptr<LogReader> qlog);

 private:
  StreamManager();
  AbstractStream *stream_ = nullptr;
};
