#include "trc_log_stream.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include "common/timing.h"
#include "modules/settings/settings.h"

// PEAK TRC format (two version families):
//
// Version 1.x  — millisecond offset from file start, single channel:
//   ;$FILEVERSION=1.1
//   ;$STARTTIME=43209.5674  (Windows OLE Automation Date, optional)
//      1)       0.000  DT  0CF  8  01 02 03 04 05 06 07 08
//
// Version 2.x  — hh:mm:ss.sss absolute wall-clock, optional channel column:
//   ;$FILEVERSION=2.0
//   ;$STARTTIME=43209.5674
//      1  00:00:00.0000  1  DT  0CF  8  01 02 03 04 05 06 07 08
//      2  00:00:00.0100  1  DT  1A0  8  00 7E ...
//
// Only "DT" (data) frames are imported. Extended IDs and 8-char hex IDs are
// both accepted. FD frames ("FD", "FB") are treated as regular DT frames.
// Other message types (ER, ST, EC …) are silently skipped.

namespace {
struct ParsedFrame {
  uint64_t rel_ns;
  uint32_t address;
  uint8_t bus;
  std::vector<uint8_t> data;
};

// Parse a v1.x timestamp string (decimal milliseconds) → nanoseconds.
// e.g. "12345.678" → 12345678000
uint64_t parseMsOffset(const QString& s) {
  return static_cast<uint64_t>(s.toDouble() * 1e6);
}

// Parse a v2.x timestamp string (hh:mm:ss.sss or hh:mm:ss.ssss) → nanoseconds.
uint64_t parseHms(const QString& s) {
  // Expected format: "HH:MM:SS.sss" (variable decimal digits)
  const QStringList parts = s.split(':');
  if (parts.size() != 3) return 0;
  double secs = parts[0].toUInt() * 3600.0 + parts[1].toUInt() * 60.0 + parts[2].toDouble();
  return static_cast<uint64_t>(secs * 1e9);
}
}  // namespace

TrcLogStream::TrcLogStream(QObject* parent, const QStringList& file_paths)
    : AbstractStream(parent), file_paths_(file_paths) {
  begin_mono_ns_ = nanos_since_boot();

  // v1.x: number) offset_ms  TYPE  ID  DLC  B0 B1 ...
  static const QRegularExpression kV1Re(
      R"(^\s*\d+\)\s+([\d.]+)\s+(?:DT|FD|FB)\s+([0-9A-Fa-f]+)\s+(\d+)\s+((?:[0-9A-Fa-f]{2}\s*)*))");

  // v2.x: number  hh:mm:ss.sss  [channel]  TYPE  ID  DLC  B0 B1 ...
  // The channel column is optional (some loggers omit it).
  static const QRegularExpression kV2Re(
      R"(^\s*\d+\s+([\d:]+\.[\d]+)\s+(?:(\d+)\s+)?(?:DT|FD|FB)\s+([0-9A-Fa-f]+)\s+(\d+)\s+((?:[0-9A-Fa-f]{2}\s*)*))");

  std::vector<ParsedFrame> parsed;

  for (const QString& file_path : file_paths_) {
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      qWarning() << "TrcLogStream: failed to open" << file_path;
      continue;
    }

    // Detect version from header.
    int version_major = 1;
    QTextStream ts(&file);
    std::vector<ParsedFrame> file_frames;

    while (!ts.atEnd()) {
      QString line = ts.readLine();

      // Header lines start with ';'
      if (line.startsWith(';')) {
        if (line.contains("$FILEVERSION=2")) version_major = 2;
        continue;
      }
      if (line.trimmed().isEmpty()) continue;

      ParsedFrame f;
      f.bus = 0;

      if (version_major >= 2) {
        auto m = kV2Re.match(line);
        if (!m.hasMatch()) continue;
        f.rel_ns = parseHms(m.captured(1));
        // capture(2) is optional channel (1-based → 0-based)
        if (!m.captured(2).isEmpty()) {
          f.bus = static_cast<uint8_t>(std::max(0, m.captured(2).toInt() - 1));
        }
        f.address = m.captured(3).toUInt(nullptr, 16);
        uint8_t dlc = m.captured(4).toUInt();
        const QStringList bytes = m.captured(5).trimmed().split(' ', Qt::SkipEmptyParts);
        f.data.reserve(dlc);
        for (int i = 0; i < std::min<int>(dlc, bytes.size()); ++i) {
          f.data.push_back(static_cast<uint8_t>(bytes[i].toUInt(nullptr, 16)));
        }
      } else {
        auto m = kV1Re.match(line);
        if (!m.hasMatch()) continue;
        f.rel_ns = parseMsOffset(m.captured(1));
        f.address = m.captured(2).toUInt(nullptr, 16);
        uint8_t dlc = m.captured(3).toUInt();
        const QStringList bytes = m.captured(4).trimmed().split(' ', Qt::SkipEmptyParts);
        f.data.reserve(dlc);
        for (int i = 0; i < std::min<int>(dlc, bytes.size()); ++i) {
          f.data.push_back(static_cast<uint8_t>(bytes[i].toUInt(nullptr, 16)));
        }
      }

      file_frames.push_back(std::move(f));
    }

    if (file_frames.empty()) continue;

    // Stitch split files: if timestamps restart, shift to follow previous end.
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

TrcLogStream::~TrcLogStream() {
  if (playback_thread_) {
    playback_thread_->requestInterruption();
    playback_thread_->quit();
    playback_thread_->wait();
  }
}

void TrcLogStream::start() {
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

void TrcLogStream::seekTo(double sec) {
  seek_to_.store(std::max(0.0, sec));
  emit seekedTo(sec);
}

void TrcLogStream::pause(bool pause) {
  paused_.store(pause);
  emit(pause ? paused() : resume());
}

void TrcLogStream::playbackThread() {
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
