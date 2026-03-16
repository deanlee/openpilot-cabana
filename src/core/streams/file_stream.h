#pragma once

#include <QStringList>
#include <QThread>
#include <atomic>
#include <condition_variable>
#include <mutex>

#include "abstract_stream.h"

class FileStream : public AbstractStream {
  Q_OBJECT

 public:
  FileStream(QObject* parent, const QStringList& file_paths);
  ~FileStream();

  void start() override;
  bool liveStreaming() const override { return false; }
  QString routeName() const override;
  double minSeconds() const override { return 0; }
  double maxSeconds() const override { return duration_s_; }
  uint64_t beginMonoNs() const override { return begin_mono_ns_; }
  void seekTo(double sec) override;
  bool isPaused() const override { return paused_; }
  void pause(bool pause) override;
  void setSpeed(float speed) override;
  double getSpeed() const override { return speed_; }

 protected:
  QStringList file_paths_;
  uint64_t begin_mono_ns_ = 0;
  double duration_s_ = 0;

 private:
  void playbackThread();

  QThread* playback_thread_ = nullptr;
  std::atomic<double> seek_to_{-1.0};
  std::atomic<bool> paused_{false};
  std::atomic<float> speed_{1.0f};
  std::mutex pause_mutex_;
  std::condition_variable pause_cv_;
};
