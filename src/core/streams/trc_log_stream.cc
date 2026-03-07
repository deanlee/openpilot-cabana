#include "trc_log_stream.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include "common/timing.h"

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
    : FileStream(parent, file_paths) {

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
