#include "dbc_file.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace dbc {

static QRegularExpression RE_SIGNAL(
  R"(^SG_\s+(?<name>\w+)\s*(?<mux>M|m\d+)?\s*:\s*(?<start>\d+)\|(?<size>\d+)@(?<endian>[01])(?<sign>[\+-])\s*\((?<factor>[0-9.+\-eE]+),(?<offset>[0-9.+\-eE]+)\)\s*\[(?<min>[0-9.+\-eE]+)\|(?<max>[0-9.+\-eE]+)\]\s*\"(?<unit>.*)\"\s*(?<receiver>.*))"
);
static QRegularExpression sgm_regexp(
  R"(^SG_ (\w+) (\w+) *: (\d+)\|(\d+)@(\d+)([\+|\-]) \(([0-9.+\-eE]+),([0-9.+\-eE]+)\) \[([0-9.+\-eE]+)\|([0-9.+\-eE]+)\] \"(.*)\" (.*))"
);
static QRegularExpression RE_MESSAGE(
  R"(^BO_ (?<address>\w+) (?<name>\w+) *: (?<size>\w+) (?<transmitter>\w+))"
);
static const QRegularExpression RE_COMMENT(R"(CM_\s+(BO_|SG_)\s+(\d+)\s*(\w+)?\s*\"(.*)\"\s*;)", QRegularExpression::DotMatchesEverythingOption);
static const QRegularExpression RE_VALUE_HEADER(R"(VAL_\s+(\d+)\s+(\w+))");
static const QRegularExpression RE_VALUE_PAIR(R"((-?\d+)\s+\"([^\"]*)\")");

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
  auto match = RE_MESSAGE.match(line);
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
  if (!current_msg) {
    throw std::runtime_error("Signal defined before any Message (BO_)");
  }

  auto match = RE_SIGNAL.match(line);
  if (!match.hasMatch()) {
    throw std::runtime_error("Invalid SG_ line format");
  }

  QString name = match.captured("name");
  if (current_msg->sig(name) != nullptr) {
    throw std::runtime_error(QString("Duplicate signal name: %1").arg(name).toStdString());
  }

  dbc::Signal s{};
  s.name = name;

  // Handle Multiplexing logic
  QString mux = match.captured("mux");
  if (mux == "M") {
    if (++multiplexor_cnt >= 2) {
      throw std::runtime_error("Multiple multiplexor switch signals (M) found in one message");
    }
    s.type = dbc::Signal::Type::Multiplexor;
  } else if (mux.startsWith('m')) {
    s.type = dbc::Signal::Type::Multiplexed;
    s.multiplex_value = mux.mid(1).toInt();
  } else {
    s.type = dbc::Signal::Type::Normal;
  }

  // Bit layout and Encoding
  s.start_bit        = match.captured("start").toInt();
  s.size             = match.captured("size").toInt();
  s.is_little_endian = (match.captured("endian") == "1");
  s.is_signed        = (match.captured("sign") == "-");

  // Physical range and Factor
  s.factor = match.captured("factor").toDouble();
  s.offset = match.captured("offset").toDouble();
  s.min    = match.captured("min").toDouble();
  s.max    = match.captured("max").toDouble();

  // Metadata
  s.unit          = match.captured("unit");
  s.receiver_name = match.captured("receiver").trimmed();

  current_msg->sigs.push_back(new dbc::Signal(s));
}

void File::parseComment(const QString& line, QTextStream& stream) {
  QString raw = line;
  // Consume stream until the entry is closed by a semicolon
  while (!raw.endsWith(';') && !stream.atEnd()) {
    raw += "\n" + stream.readLine();
  }

  auto match = RE_COMMENT.match(raw);
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
  auto header_match = RE_VALUE_HEADER.match(line);
  if (!header_match.hasMatch()) return;

  uint32_t addr = header_match.captured(1).toUInt();
  QString sig_name = header_match.captured(2);

  if (auto s = signal(addr, sig_name)) {
    s->value_table.clear();

    // Iterate through all matches in the line
    auto it = RE_VALUE_PAIR.globalMatch(line);
    while (it.hasNext()) {
      auto match = it.next();
      s->value_table.push_back({match.captured(1).toDouble(), match.captured(2)});
    }
  }
}

QString File::generateGetDBC() {
  QString body, comments, value_tables;

  // Use QTextStream for efficient buffer management
  QTextStream body_stream(&body);
  QTextStream comm_stream(&comments);
  QTextStream val_stream(&value_tables);

  for (const auto& [address, m] : msgs) {
    // 1. Generate Message (BO_)
    const QString transmitter = m.transmitter.isEmpty() ? DEFAULT_NODE_NAME : m.transmitter;
    body_stream << "BO_ " << address << " " << m.name << ": " << m.size << " " << transmitter << "\n";

    // 2. Generate Message Comment
    if (!m.comment.isEmpty()) {
      comm_stream << "CM_ BO_ " << address << " \"" << QString(m.comment).replace("\"", "\\\"") << "\";\n";
    }

    for (auto sig : m.getSignals()) {
      // 3. Generate Signal (SG_)
      QString mux;
      if (sig->type == dbc::Signal::Type::Multiplexor) {
        mux = "M ";
      } else if (sig->type == dbc::Signal::Type::Multiplexed) {
        mux = QString("m%1 ").arg(sig->multiplex_value);
      }

      body_stream << " SG_ " << sig->name << " " << mux << ": "
                  << sig->start_bit << "|" << sig->size << "@"
                  << (sig->is_little_endian ? '1' : '0') << (sig->is_signed ? '-' : '+')
                  << " (" << doubleToString(sig->factor) << "," << doubleToString(sig->offset) << ") ["
                  << doubleToString(sig->min) << "|" << doubleToString(sig->max) << "] \""
                  << sig->unit << "\" "
                  << (sig->receiver_name.isEmpty() ? DEFAULT_NODE_NAME : sig->receiver_name) << "\n";

      // 4. Generate Signal Comment
      if (!sig->comment.isEmpty()) {
        comm_stream << "CM_ SG_ " << address << " " << sig->name
                    << " \"" << QString(sig->comment).replace("\"", "\\\"") << "\";\n";
      }

      // 5. Generate Value Table (VAL_)
      if (!sig->value_table.empty()) {
        val_stream << "VAL_ " << address << " " << sig->name;
        for (const auto& [val, desc] : sig->value_table) {
          val_stream << " " << val << " \"" << desc << "\"";
        }
        val_stream << ";\n";
      }
    }
    body_stream << "\n";
  }

  // Combine components in standard DBC order: Header -> BO/SG -> CM -> VAL
  return header + body + comments + value_tables;
}

} // namespace dbc
