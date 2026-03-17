#include "candump_log_stream.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

CandumpLogStream::CandumpLogStream(QObject* parent, const QStringList& file_paths)
    : FileStream(parent, file_paths) {
  loadParsedFiles();
}

std::vector<ParsedCanFrame> CandumpLogStream::parseFile(const QString& file_path) {
  // candump -l format:  (1234567890.654321) can0 1A2#DEADBEEF
  static const QRegularExpression kFrameRe(
      R"(^\((\d+\.\d+)\)\s+(\w+)\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]*))");

  QFile file(file_path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qWarning() << "CandumpLogStream: failed to open" << file_path;
    return {};
  }

  // Track the first timestamp to normalize to relative nanoseconds.
  uint64_t t0_ns = 0;
  bool have_t0 = false;

  std::vector<ParsedCanFrame> frames;
  QTextStream ts(&file);
  while (!ts.atEnd()) {
    QString line = ts.readLine();
    if (line.isEmpty() || line.startsWith('#')) continue;

    auto m = kFrameRe.match(line);
    if (!m.hasMatch()) continue;

    uint64_t abs_ns = static_cast<uint64_t>(m.captured(1).toDouble() * 1e9);
    if (!have_t0) {
      t0_ns = abs_ns;
      have_t0 = true;
    }

    ParsedCanFrame f{};
    f.rel_ns = abs_ns - t0_ns;

    // Stable interface→bus mapping across files
    const QString iface = m.captured(2);
    auto it = iface_map_.find(iface);
    if (it != iface_map_.end()) {
      f.bus = it.value();
    } else {
      f.bus = static_cast<uint8_t>(iface_map_.size());
      iface_map_.insert(iface, f.bus);
    }

    f.address = m.captured(3).toUInt(nullptr, 16);

    // Data is packed hex (no spaces): "DEADBEEF" → {0xDE, 0xAD, 0xBE, 0xEF}
    const QString hex = m.captured(4);
    f.size = static_cast<uint8_t>(hex.size() / 2);
    for (int i = 0; i < f.size; ++i) {
      f.data[i] = static_cast<uint8_t>(hex.mid(i * 2, 2).toUInt(nullptr, 16));
    }
    frames.push_back(f);
  }
  return frames;
}
