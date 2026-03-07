#include "candump_log_stream.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include "common/timing.h"
#include "modules/settings/settings.h"

namespace {
struct ParsedFrame {
  uint64_t abs_ns;   // absolute timestamp in nanoseconds (from file)
  uint32_t address;
  uint8_t bus;
  std::vector<uint8_t> data;
};
}  // namespace

CandumpLogStream::CandumpLogStream(QObject* parent, const QStringList& file_paths)
    : AbstractStream(parent), file_paths_(file_paths) {
  begin_mono_ns_ = nanos_since_boot();

  // candump -l format:  (1234567890.654321) can0 1A2#DEADBEEF
  //   timestamp : absolute Unix epoch seconds with microsecond precision
  //   interface : CAN interface name (can0, vcan0, …) — mapped to bus index
  //   id#data   : hex CAN ID followed by packed hex data bytes
  // FD frames use a double-hash (##), remote frames append 'R' — both are
  // handled gracefully: FD data is silently ignored, remotes have empty data.
  static const QRegularExpression kFrameRe(
      R"(^\((\d+\.\d+)\)\s+(\w+)\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]*))");

  // Interface→bus map is shared across all files so the same interface always
  // gets the same bus number across a multi-file session.
  QHash<QString, uint8_t> iface_map;
  auto bus_for = [&](const QString& iface) -> uint8_t {
    auto it = iface_map.find(iface);
    if (it != iface_map.end()) return it.value();
    uint8_t idx = static_cast<uint8_t>(iface_map.size());
    iface_map.insert(iface, idx);
    return idx;
  };

  std::vector<ParsedFrame> parsed;
  for (const QString& file_path : file_paths_) {
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      qWarning() << "CandumpLogStream: failed to open" << file_path;
      continue;
    }

    QTextStream ts(&file);
    while (!ts.atEnd()) {
      QString line = ts.readLine();
      if (line.isEmpty() || line.startsWith('#')) continue;

      auto m = kFrameRe.match(line);
      if (!m.hasMatch()) continue;

      ParsedFrame f;
      f.abs_ns = static_cast<uint64_t>(m.captured(1).toDouble() * 1e9);
      f.bus = bus_for(m.captured(2));
      f.address = m.captured(3).toUInt(nullptr, 16);

      // Data is packed hex (no spaces): "DEADBEEF" → {0xDE, 0xAD, 0xBE, 0xEF}
      const QString hex = m.captured(4);
      const int nbytes = hex.size() / 2;
      f.data.reserve(nbytes);
      for (int i = 0; i < nbytes; ++i) {
        f.data.push_back(static_cast<uint8_t>(hex.mid(i * 2, 2).toUInt(nullptr, 16)));
      }
      parsed.push_back(std::move(f));
    }
  }

  if (parsed.empty()) return;

  // Sort by absolute timestamp (handles files provided out of order, and
  // merges files from the same session whose timestamps are already monotone).
  std::sort(parsed.begin(), parsed.end(),
            [](const ParsedFrame& a, const ParsedFrame& b) { return a.abs_ns < b.abs_ns; });

  // Normalise to relative nanoseconds from the first frame.
  const uint64_t t0_ns = parsed.front().abs_ns;

  std::vector<const CanEvent*> events;
  events.reserve(parsed.size());
  for (const auto& f : parsed) {
    uint64_t rel_ns = f.abs_ns - t0_ns;
    events.push_back(newEvent(begin_mono_ns_ + rel_ns, f.bus, f.address,
                              f.data.data(), static_cast<uint8_t>(f.data.size())));
  }

  duration_s_ = (events.back()->mono_ns - begin_mono_ns_) / 1e9;
  mergeEvents(events);
}

CandumpLogStream::~CandumpLogStream() {
  if (playback_thread_) {
    playback_thread_->requestInterruption();
    playback_thread_->quit();
    playback_thread_->wait();
  }
}

void CandumpLogStream::start() {
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

void CandumpLogStream::seekTo(double sec) {
  seek_to_.store(std::max(0.0, sec));
  emit seekedTo(sec);
}

void CandumpLogStream::pause(bool pause) {
  paused_.store(pause);
  emit(pause ? paused() : resume());
}

void CandumpLogStream::playbackThread() {
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
