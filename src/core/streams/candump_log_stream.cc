#include "candump_log_stream.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QTextStream>

#include "common/timing.h"

namespace {
struct ParsedFrame {
  uint64_t abs_ns;   // absolute timestamp in nanoseconds (from file)
  uint32_t address;
  uint8_t bus;
  std::vector<uint8_t> data;
};
}  // namespace

CandumpLogStream::CandumpLogStream(QObject* parent, const QStringList& file_paths)
    : FileStream(parent, file_paths) {

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
