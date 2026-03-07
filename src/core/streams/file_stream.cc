#include "file_stream.h"

#include <QThread>
#include <QTimer>
#include <algorithm>
#include <set>

#include "common/timing.h"
#include "modules/settings/settings.h"

FileStream::FileStream(QObject* parent, const QStringList& file_paths)
    : AbstractStream(parent), file_paths_(file_paths) {
  begin_mono_ns_ = nanos_since_boot();
}

FileStream::~FileStream() {
  if (playback_thread_) {
    playback_thread_->requestInterruption();
    playback_thread_->quit();
    playback_thread_->wait();
  }
}

QString FileStream::routeName() const {
  return file_paths_.size() == 1 ? file_paths_.first() : tr("%1 files").arg(file_paths_.size());
}

void FileStream::start() {
  if (all_events_.empty()) return;

  auto* timer = new QTimer(this);
  timer->setInterval(1000 / settings.fps);
  connect(timer, &QTimer::timeout, this, [this]() { commitSnapshots(); });
  connect(&settings, &Settings::changed, this, [timer]() {
    timer->setInterval(1000 / settings.fps);
  });
  timer->start();

  playback_thread_ = QThread::create([this]() { playbackThread(); });
  playback_thread_->setParent(this);
  connect(playback_thread_, &QThread::finished, playback_thread_, &QThread::deleteLater);
  playback_thread_->start();
}

void FileStream::seekTo(double sec) {
  seek_to_.store(std::max(0.0, sec));
  emit seekedTo(sec);
}

void FileStream::pause(bool pause) {
  paused_.store(pause);
  emit(pause ? paused() : resume());
}

void FileStream::playbackThread() {
  size_t idx = 0;
  uint64_t playback_start_ns = nanos_since_boot();
  uint64_t file_start_ns = 0;

  auto do_seek = [&](double sec) {
    uint64_t target_ns = begin_mono_ns_ + static_cast<uint64_t>(sec * 1e9);
    auto it = std::lower_bound(all_events_.begin(), all_events_.end(), target_ns,
                               [](const CanEvent* e, uint64_t t) { return e->mono_ns < t; });
    idx = std::distance(all_events_.begin(), it);
    file_start_ns = (idx < all_events_.size()) ? all_events_[idx]->mono_ns : begin_mono_ns_;
    playback_start_ns = nanos_since_boot();
  };

  do_seek(0.0);

  while (!QThread::currentThread()->isInterruptionRequested()) {
    double requested = seek_to_.exchange(-1.0);
    if (requested >= 0.0) {
      do_seek(requested);
      std::set<MessageId> empty;
      emit snapshotsUpdated(&empty, true);
    }

    if (paused_.load()) {
      QThread::msleep(33);
      playback_start_ns = nanos_since_boot();
      file_start_ns = (idx < all_events_.size()) ? all_events_[idx]->mono_ns : file_start_ns;
      continue;
    }

    if (idx >= all_events_.size()) {
      QThread::msleep(100);
      continue;
    }

    const float spd = speed_.load();
    const uint64_t wall_elapsed = static_cast<uint64_t>((nanos_since_boot() - playback_start_ns) * spd);
    const uint64_t file_elapsed = all_events_[idx]->mono_ns - file_start_ns;

    if (file_elapsed > wall_elapsed) {
      uint64_t wait_ns = std::min<uint64_t>((file_elapsed - wall_elapsed) / spd, 50'000'000ULL);
      QThread::usleep(wait_ns / 1000);
      continue;
    }

    while (idx < all_events_.size()) {
      if (all_events_[idx]->mono_ns - file_start_ns > wall_elapsed) break;
      const CanEvent* e = all_events_[idx++];
      processNewMessage({e->src, e->address}, e->mono_ns, e->dat, e->size);
    }

    if (idx > 0) {
      emit seeking(toSeconds(all_events_[idx - 1]->mono_ns));
    }
  }
}
