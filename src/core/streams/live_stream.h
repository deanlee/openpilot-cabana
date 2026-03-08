#pragma once

#include <QBasicTimer>
#include <algorithm>
#include <memory>
#include <vector>

#include "abstract_stream.h"

class LiveStream : public AbstractStream {
  Q_OBJECT

 public:
  LiveStream(QObject* parent);
  ~LiveStream() override;
  void start() override;
  void stop();
  QDateTime beginDateTime() const override { return begin_date_time_; }
  uint64_t beginMonoNs() const override { return begin_ns_; }
  double maxSeconds() const override { return std::max(1.0, (latest_ns_ - begin_ns_) / 1e9); }
  void setSpeed(float speed) override;
  double getSpeed() const override { return speed_; }
  bool isPaused() const override { return paused_; }
  void pause(bool pause) override;
  void seekTo(double sec) override;

 protected:
  virtual void streamThread() = 0;
  void handleEvent(kj::ArrayPtr<capnp::word> event);

 private:
  void startFrameTimer();
  void timerEvent(QTimerEvent* event) override;
  void drainQueue();
  void advancePlayback();

  // Reset the playback anchor to the current cursor position.
  // Called on speed change, pause/unpause, and seek.
  void resetAnchor();
  uint64_t playbackTarget() const;

  // Thread communication
  std::mutex recv_mutex_;
  QThread* stream_thread_ = nullptr;
  std::vector<const CanEvent*> recv_queue_;

  QBasicTimer frame_timer_;
  QDateTime begin_date_time_;

  // Timestamps (nanoseconds)
  uint64_t begin_ns_ = 0;    // First event ever (origin for seconds conversion)
  uint64_t latest_ns_ = 0;   // Most recent received event
  uint64_t cursor_ns_ = 0;   // Current playback position

  // Playback clock: target_can = anchor_can + (wall_now - anchor_wall) * speed
  uint64_t anchor_wall_ns_ = 0;
  uint64_t anchor_can_ns_ = 0;
  double speed_ = 1.0;
  bool paused_ = false;
  bool at_live_edge_ = true;  // At speed=1.0, skip clock math and process all events

  struct Logger;
  std::unique_ptr<Logger> logger_;
};
