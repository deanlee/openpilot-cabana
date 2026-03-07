#pragma once

#include <QStringList>
#include <QThread>
#include <atomic>

#include "abstract_stream.h"

class CandumpLogStream : public AbstractStream {
  Q_OBJECT

 public:
  CandumpLogStream(QObject* parent, const QStringList& file_paths);
  ~CandumpLogStream();

  void start() override;
  bool liveStreaming() const override { return false; }
  QString routeName() const override {
    return file_paths_.size() == 1 ? file_paths_.first()
                                   : tr("%1 files").arg(file_paths_.size());
  }
  double minSeconds() const override { return 0; }
  double maxSeconds() const override { return duration_s_; }
  uint64_t beginMonoNs() const override { return begin_mono_ns_; }
  void seekTo(double sec) override;
  bool isPaused() const override { return paused_; }
  void pause(bool pause) override;
  void setSpeed(float speed) override { speed_ = speed; }
  double getSpeed() override { return speed_; }

 private:
  void playbackThread();

  QStringList file_paths_;
  uint64_t begin_mono_ns_ = 0;
  double duration_s_ = 0;

  QThread* playback_thread_ = nullptr;
  std::atomic<double> seek_to_{-1.0};
  std::atomic<bool> paused_{false};
  std::atomic<float> speed_{1.0f};
};
