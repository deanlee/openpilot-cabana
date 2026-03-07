#include "asc_log_stream.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include "common/timing.h"
#include "modules/settings/settings.h"

namespace {
// Temporary struct used only during file parsing.
struct ParsedFrame {
  uint64_t rel_ns;   // file-relative nanoseconds
  uint32_t address;
  uint8_t bus;
  std::vector<uint8_t> data;
};
}  // namespace

AscLogStream::AscLogStream(QObject* parent, const QStringList& file_paths)
    : AbstractStream(parent), file_paths_(file_paths) {
  begin_mono_ns_ = nanos_since_boot();

  // Parse all .asc files into ParsedFrame, apply timestamp stitching for
  // split files, then allocate CanEvent objects and call mergeEvents() so the
  // full event history is immediately available (charts, message list, etc.).
  static const QRegularExpression kFrameRe(
      // Matches: timestamp  channel  id  dir  d  dlc  data...
      //   e.g.  0.123456 1  0CF  Rx   d 8  01 02 03 04 05 06 07 08
      R"(^\s*([\d.]+)\s+(\d+)\s+([0-9A-Fa-f]+)\s+\w+\s+d\s+(\d+)\s+((?:[0-9A-Fa-f]{2}\s*)*))");

  std::vector<ParsedFrame> parsed;
  for (const QString& file_path : file_paths_) {
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      qWarning() << "AscLogStream: failed to open" << file_path;
      continue;
    }

    std::vector<ParsedFrame> file_frames;
    QTextStream ts(&file);
    while (!ts.atEnd()) {
      QString line = ts.readLine();
      if (line.startsWith("//") || line.startsWith("base") || line.isEmpty()) continue;

      auto m = kFrameRe.match(line);
      if (!m.hasMatch()) continue;

      ParsedFrame f;
      f.rel_ns = static_cast<uint64_t>(m.captured(1).toDouble() * 1e9);
      f.bus = static_cast<uint8_t>(m.captured(2).toUInt() - 1);  // ASC channels are 1-based
      f.address = m.captured(3).toUInt(nullptr, 16);
      uint8_t dlc = m.captured(4).toUInt();

      const QStringList bytes = m.captured(5).trimmed().split(' ', Qt::SkipEmptyParts);
      f.data.reserve(dlc);
      for (int i = 0; i < std::min<int>(dlc, bytes.size()); ++i) {
        f.data.push_back(static_cast<uint8_t>(bytes[i].toUInt(nullptr, 16)));
      }
      file_frames.push_back(std::move(f));
    }

    if (file_frames.empty()) continue;

    // Stitch: if this file's timestamps restart (overlap with already-parsed frames),
    // shift the new frames to follow the previous file's end by 1 ms.
    if (!parsed.empty() && file_frames.front().rel_ns <= parsed.back().rel_ns) {
      uint64_t shift = parsed.back().rel_ns + 1'000'000ULL - file_frames.front().rel_ns;
      for (auto& f : file_frames) f.rel_ns += shift;
    }

    parsed.insert(parsed.end(),
                  std::make_move_iterator(file_frames.begin()),
                  std::make_move_iterator(file_frames.end()));
  }

  // Sort handles files provided out of order.
  std::sort(parsed.begin(), parsed.end(),
            [](const ParsedFrame& a, const ParsedFrame& b) { return a.rel_ns < b.rel_ns; });

  // Allocate CanEvent objects (owned by the base-class event_buffer_) and
  // call mergeEvents() which populates all_events_, the per-message events
  // map, and emits eventsMerged() so the rest of the UI can populate itself.
  std::vector<const CanEvent*> events;
  events.reserve(parsed.size());
  for (const auto& f : parsed) {
    events.push_back(newEvent(begin_mono_ns_ + f.rel_ns, f.bus, f.address,
                              f.data.data(), static_cast<uint8_t>(f.data.size())));
  }

  if (!events.empty()) {
    duration_s_ = (events.back()->mono_ns - begin_mono_ns_) / 1e9;
    mergeEvents(events);
  }
}

AscLogStream::~AscLogStream() {
  if (playback_thread_) {
    playback_thread_->requestInterruption();
    playback_thread_->quit();
    playback_thread_->wait();
  }
}

void AscLogStream::start() {
  if (all_events_.empty()) return;

  // Drive UI updates at settings.fps on the main thread, decoupled from
  // event dispatch density (mirrors how ReplayStream uses its ui_update_timer).
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

void AscLogStream::seekTo(double sec) {
  seek_to_.store(std::max(0.0, sec));
  emit seekedTo(sec);
}

void AscLogStream::pause(bool pause) {
  paused_.store(pause);
  emit(pause ? paused() : resume());
}

void AscLogStream::playbackThread() {
  // Real-time replay (scaled by speed_) of all_events_, emitting live snapshot
  // updates via processNewMessage() + commitSnapshots() for the current position.
  size_t idx = 0;
  uint64_t playback_start_ns = nanos_since_boot();
  uint64_t file_start_ns = 0;  // mono_ns of first event at current seek position

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
    // Handle pending seek.
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
      // Not yet time for the next event; sleep up to 50 ms.
      uint64_t wait_ns = std::min<uint64_t>((file_elapsed - wall_elapsed) / spd, 50'000'000ULL);
      QThread::usleep(wait_ns / 1000);
      continue;
    }

    // Dispatch all events that are due.
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

