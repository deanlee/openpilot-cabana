#include "dbc_file.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace dbc {

File::File(const QString &dbc_file_name) {
  QFile file(dbc_file_name);
  if (file.open(QIODevice::ReadOnly)) {
    name_ = QFileInfo(dbc_file_name).baseName();
    filename = dbc_file_name;
    parse(file.readAll());
  } else {
    throw std::runtime_error("Failed to open file.");
  }
}

File::File(const QString &name, const QString &content) : name_(name), filename("") {
  parse(content);
}

bool File::save() {
  assert(!filename.isEmpty());
  return writeContents(filename);
}

bool File::saveAs(const QString &new_filename) {
  filename = new_filename;
  return save();
}

bool File::writeContents(const QString &fn) {
  QFile file(fn);
  if (file.open(QIODevice::WriteOnly)) {
    return file.write(generateGetDBC().toUtf8()) >= 0;
  }
  return false;
}

void File::updateMsg(const MessageId &id, const QString &name, uint32_t size, const QString &node, const QString &comment) {
  auto &m = msgs[id.address];
  m.address = id.address;
  m.name = name;
  m.size = size;
  m.transmitter = node.isEmpty() ? DEFAULT_NODE_NAME : node;
  m.comment = comment;
}

dbc::Msg *File::msg(uint32_t address) {
  auto it = msgs.find(address);
  return it != msgs.end() ? &it->second : nullptr;
}

dbc::Msg *File::msg(const QString &name) {
  auto it = std::find_if(msgs.begin(), msgs.end(), [&name](auto &m) { return m.second.name == name; });
  return it != msgs.end() ? &(it->second) : nullptr;
}

dbc::Signal *File::signal(uint32_t address, const QString &name) {
  auto m = msg(address);
  return m ? (dbc::Signal *)m->sig(name) : nullptr;
}

void File::parse(const QString &content) {
  msgs.clear();

  int line_num = 0;
  QString line;
  dbc::Msg *current_msg = nullptr;
  int multiplexor_cnt = 0;
  bool seen_first = false;
  QTextStream stream((QString *)&content);

  while (!stream.atEnd()) {
    ++line_num;
    QString raw_line = stream.readLine();
    line = raw_line.trimmed();

    bool seen = true;
    try {
      if (line.startsWith("BO_ ")) {
        multiplexor_cnt = 0;
        current_msg = parseBO(line);
      } else if (line.startsWith("SG_ ")) {
        parseSG(line, current_msg, multiplexor_cnt);
      } else if (line.startsWith("VAL_ ")) {
        parseVAL(line);
      } else if (line.startsWith("CM_ BO_")) {
        parseComment(line, stream);
      } else if (line.startsWith("CM_ SG_ ")) {
        parseComment(line, stream);
      } else {
        seen = false;
      }
    } catch (std::exception &e) {
      throw std::runtime_error(QString("[%1:%2]%3: %4").arg(filename).arg(line_num).arg(e.what()).arg(line).toStdString());
    }

    if (seen) {
      seen_first = true;
    } else if (!seen_first) {
      header += raw_line + "\n";
    }
  }

  for (auto &[_, m] : msgs) {
    m.update();
  }
}

dbc::Msg *File::parseBO(const QString &line) {
  static QRegularExpression bo_regexp(R"(^BO_ (?<address>\w+) (?<name>\w+) *: (?<size>\w+) (?<transmitter>\w+))");

  QRegularExpressionMatch match = bo_regexp.match(line);
  if (!match.hasMatch())
    throw std::runtime_error("Invalid BO_ line format");

  uint32_t address = match.captured("address").toUInt();
  if (msgs.count(address) > 0)
    throw std::runtime_error(QString("Duplicate message address: %1").arg(address).toStdString());

  // Create a new message object
  dbc::Msg *msg = &msgs[address];
  msg->address = address;
  msg->name = match.captured("name");
  msg->size = match.captured("size").toULong();
  msg->transmitter = match.captured("transmitter").trimmed();
  return msg;
}

void File::parseSG(const QString &line, dbc::Msg *current_msg, int &multiplexor_cnt) {
  static QRegularExpression sg_regexp(R"(^SG_ (\w+) *: (\d+)\|(\d+)@(\d+)([\+|\-]) \(([0-9.+\-eE]+),([0-9.+\-eE]+)\) \[([0-9.+\-eE]+)\|([0-9.+\-eE]+)\] \"(.*)\" (.*))");
  static QRegularExpression sgm_regexp(R"(^SG_ (\w+) (\w+) *: (\d+)\|(\d+)@(\d+)([\+|\-]) \(([0-9.+\-eE]+),([0-9.+\-eE]+)\) \[([0-9.+\-eE]+)\|([0-9.+\-eE]+)\] \"(.*)\" (.*))");

  if (!current_msg)
    throw std::runtime_error("No Message");

  int offset = 0;
  auto match = sg_regexp.match(line);
  if (!match.hasMatch()) {
    match = sgm_regexp.match(line);
    offset = 1;
  }
  if (!match.hasMatch())
    throw std::runtime_error("Invalid SG_ line format");

  QString name = match.captured(1);
  if (current_msg->sig(name) != nullptr)
    throw std::runtime_error("Duplicate signal name");

  dbc::Signal s{};
  if (offset == 1) {
    auto indicator = match.captured(2);
    if (indicator == "M") {
      ++multiplexor_cnt;
      // Only one signal within a single message can be the multiplexer switch.
      if (multiplexor_cnt >= 2)
        throw std::runtime_error("Multiple multiplexor");

      s.type = dbc::Signal::Type::Multiplexor;
    } else {
      s.type = dbc::Signal::Type::Multiplexed;
      s.multiplex_value = indicator.mid(1).toInt();
    }
  }
  s.name = name;
  s.start_bit = match.captured(offset + 2).toInt();
  s.size = match.captured(offset + 3).toInt();
  s.is_little_endian = match.captured(offset + 4).toInt() == 1;
  s.is_signed = match.captured(offset + 5) == "-";
  s.factor = match.captured(offset + 6).toDouble();
  s.offset = match.captured(offset + 7).toDouble();
  s.min = match.captured(8 + offset).toDouble();
  s.max = match.captured(9 + offset).toDouble();
  s.unit = match.captured(10 + offset);
  s.receiver_name = match.captured(11 + offset).trimmed();
  current_msg->sigs.push_back(new dbc::Signal(s));
}

void File::parseComment(const QString& line, QTextStream& stream) {
  QString raw = line;
  // Consume stream until the entry is closed by a semicolon
  while (!raw.endsWith(';') && !stream.atEnd()) {
    raw += "\n" + stream.readLine();
  }

  // Capture 1: BO_|SG_, Capture 2: Address, Capture 3: Signal Name (Optional), Capture 4: Comment
  static const QRegularExpression re_cm(R"(CM_\s+(BO_|SG_)\s+(\d+)\s*(\w+)?\s*\"(.*)\"\s*;)",
                                        QRegularExpression::DotMatchesEverythingOption);

  auto match = re_cm.match(raw);
  if (!match.hasMatch()) return;

  uint32_t addr = match.captured(2).toUInt();
  QString comment = match.captured(4).replace("\\\"", "\"").trimmed();

  if (match.captured(1) == "BO_") {
    if (auto m = msg(addr)) m->comment = comment;
  } else {
    if (auto s = signal(addr, match.captured(3))) s->comment = comment;
  }
}

void File::parseVAL(const QString& line) {
  static const QRegularExpression val_header(R"(VAL_\s+(\d+)\s+(\w+))");
  // Regex 2: Match every pair of: 123 "Description Text"
  static const QRegularExpression pair_regex(R"((-?\d+)\s+\"([^\"]*)\")");

  auto header_match = val_header.match(line);
  if (!header_match.hasMatch()) return;

  uint32_t addr = header_match.captured(1).toUInt();
  QString sig_name = header_match.captured(2);

  if (auto s = signal(addr, sig_name)) {
    s->value_table.clear();

    // Iterate through all matches in the line
    auto it = pair_regex.globalMatch(line);
    while (it.hasNext()) {
      auto match = it.next();
      s->value_table.push_back({match.captured(1).toDouble(), match.captured(2)});
    }
  }
}

QString File::generateGetDBC() {
  QString dbc_string, comment, value_table;
  for (const auto &[address, m] : msgs) {
    const QString transmitter = m.transmitter.isEmpty() ? DEFAULT_NODE_NAME : m.transmitter;
    dbc_string += QString("BO_ %1 %2: %3 %4\n").arg(address).arg(m.name).arg(m.size).arg(transmitter);
    if (!m.comment.isEmpty()) {
      comment += QString("CM_ BO_ %1 \"%2\";\n").arg(address).arg(QString(m.comment).replace("\"", "\\\""));
    }
    for (auto sig : m.getSignals()) {
      QString multiplexer_indicator;
      if (sig->type == dbc::Signal::Type::Multiplexor) {
        multiplexer_indicator = "M ";
      } else if (sig->type == dbc::Signal::Type::Multiplexed) {
        multiplexer_indicator = QString("m%1 ").arg(sig->multiplex_value);
      }
      dbc_string += QString(" SG_ %1 %2: %3|%4@%5%6 (%7,%8) [%9|%10] \"%11\" %12\n")
                        .arg(sig->name)
                        .arg(multiplexer_indicator)
                        .arg(sig->start_bit)
                        .arg(sig->size)
                        .arg(sig->is_little_endian ? '1' : '0')
                        .arg(sig->is_signed ? '-' : '+')
                        .arg(doubleToString(sig->factor))
                        .arg(doubleToString(sig->offset))
                        .arg(doubleToString(sig->min))
                        .arg(doubleToString(sig->max))
                        .arg(sig->unit)
                        .arg(sig->receiver_name.isEmpty() ? DEFAULT_NODE_NAME : sig->receiver_name);
      if (!sig->comment.isEmpty()) {
        comment += QString("CM_ SG_ %1 %2 \"%3\";\n").arg(address).arg(sig->name).arg(QString(sig->comment).replace("\"", "\\\""));
      }
      if (!sig->value_table.empty()) {
        QStringList text;
        for (auto &[val, desc] : sig->value_table) {
          text << QString("%1 \"%2\"").arg(val).arg(desc);
        }
        value_table += QString("VAL_ %1 %2 %3;\n").arg(address).arg(sig->name).arg(text.join(" "));
      }
    }
    dbc_string += "\n";
  }
  return header + dbc_string + comment + value_table;
}

} // namespace dbc
