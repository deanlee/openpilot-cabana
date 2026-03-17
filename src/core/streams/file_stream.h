#pragma once

#include <QStringList>
#include <QThread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

#include "abstract_stream.h"

// Lightweight parsed frame used during file loading.
// Each file-format subclass produces these; the base class handles
// timestamp stitching, sorting, CanEvent allocation, and mergeEvents().
struct ParsedCanFrame {
  uint64_t rel_ns;    // file-relative nanoseconds (or absolute, normalized later)
  uint32_t address;
  uint8_t bus;
  uint8_t size;
  uint8_t data[64];   // inline storage avoids per-frame heap allocation
};

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
  // Subclasses implement this to parse a single file into ParsedCanFrames.
  // Timestamps should be file-relative nanoseconds (from 0).
  // The base class handles stitching, sorting, and event allocation.
  virtual std::vector<ParsedCanFrame> parseFile(const QString& file_path) = 0;

  // Call from subclass constructor after parsing to build the event timeline.
  void loadParsedFiles();

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
