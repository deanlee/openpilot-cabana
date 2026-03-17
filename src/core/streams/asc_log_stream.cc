#include "asc_log_stream.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

AscLogStream::AscLogStream(QObject* parent, const QStringList& file_paths)
    : FileStream(parent, file_paths) {
  loadParsedFiles();
}

std::vector<ParsedCanFrame> AscLogStream::parseFile(const QString& file_path) {
  // Matches: timestamp  channel  id  dir  d  dlc  data...
  //   e.g.  0.123456 1  0CF  Rx   d 8  01 02 03 04 05 06 07 08
  static const QRegularExpression kFrameRe(
      R"(^\s*([\d.]+)\s+(\d+)\s+([0-9A-Fa-f]+)\s+\w+\s+d\s+(\d+)\s+((?:[0-9A-Fa-f]{2}\s*)*))");

  QFile file(file_path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qWarning() << "AscLogStream: failed to open" << file_path;
    return {};
  }

  std::vector<ParsedCanFrame> frames;
  QTextStream ts(&file);
  while (!ts.atEnd()) {
    QString line = ts.readLine();
    if (line.startsWith("//") || line.startsWith("base") || line.isEmpty()) continue;

    auto m = kFrameRe.match(line);
    if (!m.hasMatch()) continue;

    ParsedCanFrame f{};
    f.rel_ns = static_cast<uint64_t>(m.captured(1).toDouble() * 1e9);
    f.bus = static_cast<uint8_t>(m.captured(2).toUInt() - 1);  // ASC channels are 1-based
    f.address = m.captured(3).toUInt(nullptr, 16);
    uint8_t dlc = m.captured(4).toUInt();

    const QStringList bytes = m.captured(5).trimmed().split(' ', Qt::SkipEmptyParts);
    f.size = static_cast<uint8_t>(std::min<int>(dlc, bytes.size()));
    for (int i = 0; i < f.size; ++i) {
      f.data[i] = static_cast<uint8_t>(bytes[i].toUInt(nullptr, 16));
    }
    frames.push_back(f);
  }
  return frames;
}

