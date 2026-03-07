#include "asc_log_stream.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

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
    : FileStream(parent, file_paths) {

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

