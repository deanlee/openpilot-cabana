#include "trc_log_stream.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

// PEAK TRC format (two version families):
//
// Version 1.x  — millisecond offset from file start, single channel:
//   ;$FILEVERSION=1.1
//      1)       0.000  DT  0CF  8  01 02 03 04 05 06 07 08
//
// Version 2.x  — hh:mm:ss.sss absolute wall-clock, optional channel column:
//   ;$FILEVERSION=2.0
//      1  00:00:00.0000  1  DT  0CF  8  01 02 03 04 05 06 07 08

namespace {

// Parse a v1.x timestamp string (decimal milliseconds) → nanoseconds.
uint64_t parseMsOffset(const QString& s) {
  return static_cast<uint64_t>(s.toDouble() * 1e6);
}

// Parse a v2.x timestamp string (hh:mm:ss.sss or hh:mm:ss.ssss) → nanoseconds.
uint64_t parseHms(const QString& s) {
  const QStringList parts = s.split(':');
  if (parts.size() != 3) return 0;
  double secs = parts[0].toUInt() * 3600.0 + parts[1].toUInt() * 60.0 + parts[2].toDouble();
  return static_cast<uint64_t>(secs * 1e9);
}

}  // namespace

TrcLogStream::TrcLogStream(QObject* parent, const QStringList& file_paths)
    : FileStream(parent, file_paths) {
  loadParsedFiles();
}

std::vector<ParsedCanFrame> TrcLogStream::parseFile(const QString& file_path) {
  // v1.x: number) offset_ms  TYPE  ID  DLC  B0 B1 ...
  static const QRegularExpression kV1Re(
      R"(^\s*\d+\)\s+([\d.]+)\s+(?:DT|FD|FB)\s+([0-9A-Fa-f]+)\s+(\d+)\s+((?:[0-9A-Fa-f]{2}\s*)*))");

  // v2.x: number  hh:mm:ss.sss  [channel]  TYPE  ID  DLC  B0 B1 ...
  static const QRegularExpression kV2Re(
      R"(^\s*\d+\s+([\d:]+\.[\d]+)\s+(?:(\d+)\s+)?(?:DT|FD|FB)\s+([0-9A-Fa-f]+)\s+(\d+)\s+((?:[0-9A-Fa-f]{2}\s*)*))");

  QFile file(file_path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qWarning() << "TrcLogStream: failed to open" << file_path;
    return {};
  }

  int version_major = 1;
  QTextStream ts(&file);
  std::vector<ParsedCanFrame> frames;

  while (!ts.atEnd()) {
    QString line = ts.readLine();

    if (line.startsWith(';')) {
      if (line.contains("$FILEVERSION=2")) version_major = 2;
      continue;
    }
    if (line.trimmed().isEmpty()) continue;

    ParsedCanFrame f{};

    if (version_major >= 2) {
      auto m = kV2Re.match(line);
      if (!m.hasMatch()) continue;
      f.rel_ns = parseHms(m.captured(1));
      if (!m.captured(2).isEmpty()) {
        f.bus = static_cast<uint8_t>(std::max(0, m.captured(2).toInt() - 1));
      }
      f.address = m.captured(3).toUInt(nullptr, 16);
      uint8_t dlc = m.captured(4).toUInt();
      const QStringList bytes = m.captured(5).trimmed().split(' ', Qt::SkipEmptyParts);
      f.size = static_cast<uint8_t>(std::min<int>(dlc, bytes.size()));
      for (int i = 0; i < f.size; ++i) {
        f.data[i] = static_cast<uint8_t>(bytes[i].toUInt(nullptr, 16));
      }
    } else {
      auto m = kV1Re.match(line);
      if (!m.hasMatch()) continue;
      f.rel_ns = parseMsOffset(m.captured(1));
      f.address = m.captured(2).toUInt(nullptr, 16);
      uint8_t dlc = m.captured(3).toUInt();
      const QStringList bytes = m.captured(4).trimmed().split(' ', Qt::SkipEmptyParts);
      f.size = static_cast<uint8_t>(std::min<int>(dlc, bytes.size()));
      for (int i = 0; i < f.size; ++i) {
        f.data[i] = static_cast<uint8_t>(bytes[i].toUInt(nullptr, 16));
      }
    }

    frames.push_back(f);
  }
  return frames;
}
