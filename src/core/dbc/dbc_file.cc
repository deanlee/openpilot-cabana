#include "dbc_file.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include "utils/util.h"

namespace dbc {

static const QRegularExpression RE_SIGNAL(
    R"(^SG_\s+(?<name>\w+)\s*(?<mux>M|m\d+)?\s*:\s*(?<start>\d+)\|(?<size>\d+)@(?<endian>[01])(?<sign>[\+-])\s*\((?<factor>[0-9.+\-eE]+),(?<offset>[0-9.+\-eE]+)\)\s*\[(?<min>[0-9.+\-eE]+)\|(?<max>[0-9.+\-eE]+)\]\s*\"(?<unit>.*)\"\s*(?<receiver>.*))");
static const QRegularExpression RE_MESSAGE(R"(^BO_ (?<address>\w+) (?<name>\w+) *: (?<size>\w+) (?<transmitter>\w+))");
static const QRegularExpression RE_COMMENT(
    R"(CM_\s+(?<type>BO_|SG_)\s+(?<address>\d+)\s*(?<signal>\w+)?\s*\"(?<comment>.*)\"\s*;)",
    QRegularExpression::DotMatchesEverythingOption);
static const QRegularExpression RE_VALUE_HEADER(R"(VAL_\s+(?<address>\d+)\s+(?<signal>\w+))");
static const QRegularExpression RE_VALUE_PAIR(R"((-?\d+)\s+\"([^\"]*)\")");

File::File(const QString& dbc_file_name) {
  QFile file(dbc_file_name);
  if (file.open(QIODevice::ReadOnly)) {
    name_ = QFileInfo(dbc_file_name).baseName();
    filename = dbc_file_name;
    parse(file.readAll());
  } else {
    throw std::runtime_error("Failed to open file.");
  }
}

File::File(const QString& name, const QString& content) : name_(name), filename("") { parse(content); }

bool File::save() {
  assert(!filename.isEmpty());
  return saveToFile(filename);
}

bool File::saveAs(const QString& new_filename) {
  filename = new_filename;
  return saveToFile(filename);
}

bool File::saveToFile(const QString& fn) {
  QFile file(fn);
  if (file.open(QIODevice::WriteOnly)) {
    return file.write(toDBCString().toUtf8()) >= 0;
  }
  return false;
}

void File::updateMsg(const MessageId& id, const QString& name, uint32_t size, const QString& node,
                     const QString& comment) {
  auto& m = msgs[id.address];
  m.address = id.address;
  m.name = name;
  m.size = size;
  m.transmitter = node.isEmpty() ? DEFAULT_NODE_NAME : node;
  m.comment = comment;
}

dbc::Msg* File::msg(uint32_t address) {
  auto it = msgs.find(address);
  return it != msgs.end() ? &it->second : nullptr;
}

dbc::Msg* File::msg(const QString& name) {
  auto it = std::ranges::find_if(msgs, [&name](auto& m) { return m.second.name == name; });
  return it != msgs.end() ? &(it->second) : nullptr;
}

dbc::Signal* File::signal(uint32_t address, const QString& name) {
  auto m = msg(address);
  return m ? m->sig(name) : nullptr;
}

void File::parse(QString content) {
  msgs.clear();
  header.clear();
  QTextStream stream(&content);
  int line_num = 0;
  dbc::Msg* current_msg = nullptr;
  int multiplexor_cnt = 0;
  bool seen_first = false;

  while (!stream.atEnd()) {
    ++line_num;
    const QString raw_line = stream.readLine();
    const QString line = raw_line.trimmed();

    bool matched = true;
    try {
      if (line.startsWith("BO_ ")) {
        multiplexor_cnt = 0;
        current_msg = parseBO(line);
      } else if (line.startsWith("SG_ ")) {
        parseSG(line, current_msg, multiplexor_cnt);
      } else if (line.startsWith("VAL_ ")) {
        parseVAL(line);
      } else if (line.startsWith("CM_ BO_") || line.startsWith("CM_ SG_ ")) {
        parseComment(line, stream, line_num);
      } else {
        matched = false;
      }
    } catch (std::exception& e) {
      throw std::runtime_error(
          QString("[%1:%2] %3: %4").arg(filename).arg(line_num).arg(e.what()).arg(line).toStdString());
    }

    if (matched)
      seen_first = true;
    else if (!seen_first)
      header += raw_line + '\n';
  }

  for (auto& [_, m] : msgs) m.update();
}

dbc::Msg* File::parseBO(const QString& line) {
  auto match = RE_MESSAGE.match(line);
  if (!match.hasMatch()) throw std::runtime_error("Invalid BO_ line format");

  uint32_t address = match.captured("address").toUInt();
  auto [it, inserted] = msgs.try_emplace(address);
  if (!inserted) throw std::runtime_error(QString("Duplicate message address: %1").arg(address).toStdString());

  dbc::Msg* msg = &it->second;
  msg->address = address;
  msg->name = match.captured("name");
  msg->size = match.captured("size").toULong();
  msg->transmitter = match.captured("transmitter").trimmed();
  return msg;
}

void File::parseSG(const QString& line, dbc::Msg* current_msg, int& multiplexor_cnt) {
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

  auto* s = new dbc::Signal();
  s->name = name;

  // Multiplexing
  const QString mux = match.captured("mux");
  if (mux == "M") {
    if (++multiplexor_cnt >= 2)
      throw std::runtime_error("Multiple multiplexor switch signals (M) found in one message");
    s->type = dbc::Signal::Type::Multiplexor;
  } else if (mux.startsWith('m')) {
    s->type = dbc::Signal::Type::Multiplexed;
    s->multiplex_value = mux.mid(1).toInt();
  }

  // Bit layout
  s->start_bit = match.captured("start").toInt();
  s->size = match.captured("size").toInt();
  s->is_little_endian = (match.captured("endian") == "1");
  s->is_signed = (match.captured("sign") == "-");

  // Physical value
  s->factor = match.captured("factor").toDouble();
  s->offset = match.captured("offset").toDouble();
  s->min = match.captured("min").toDouble();
  s->max = match.captured("max").toDouble();

  // Metadata
  s->unit = match.captured("unit");
  s->receiver_name = match.captured("receiver").trimmed();

  current_msg->sigs.push_back(s);
}

void File::parseComment(const QString& line, QTextStream& stream, int& line_num) {
  QString raw = line;
  while (!raw.endsWith(';') && !stream.atEnd()) {
    raw += '\n' + stream.readLine();
    ++line_num;
  }

  auto match = RE_COMMENT.match(raw);
  if (!match.hasMatch()) return;

  uint32_t addr = match.captured("address").toUInt();
  QString comment = match.captured("comment").replace("\\\"", "\"").trimmed();

  if (match.captured("type") == "BO_") {
    if (auto m = msg(addr)) m->comment = comment;
  } else {
    if (auto s = signal(addr, match.captured("signal"))) s->comment = comment;
  }
}

void File::parseVAL(const QString& line) {
  auto header_match = RE_VALUE_HEADER.match(line);
  if (!header_match.hasMatch()) return;

  uint32_t addr = header_match.captured("address").toUInt();
  QString sig_name = header_match.captured("signal");

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

QString File::toDBCString() {
  QString body, comments, value_tables;
  QTextStream body_stream(&body);
  QTextStream comm_stream(&comments);
  QTextStream val_stream(&value_tables);

  auto quoteEscape = [](const QString& s) { return QString(s).replace('"', "\\\""); };
  auto muxPrefix = [](const dbc::Signal* sig) -> QString {
    if (sig->type == dbc::Signal::Type::Multiplexor) return "M ";
    if (sig->type == dbc::Signal::Type::Multiplexed) return QString("m%1 ").arg(sig->multiplex_value);
    return {};
  };

  for (const auto& [address, m] : msgs) {
    const QString transmitter = m.transmitter.isEmpty() ? DEFAULT_NODE_NAME : m.transmitter;
    body_stream << "BO_ " << address << " " << m.name << ": " << m.size << " " << transmitter << "\n";

    if (!m.comment.isEmpty()) comm_stream << "CM_ BO_ " << address << " \"" << quoteEscape(m.comment) << "\";\n";

    for (const auto* sig : m.getSignals()) {
      body_stream << " SG_ " << sig->name << " " << muxPrefix(sig) << ": " << sig->start_bit << "|" << sig->size << "@"
                  << (sig->is_little_endian ? '1' : '0') << (sig->is_signed ? '-' : '+') << " ("
                  << utils::doubleToString(sig->factor) << "," << utils::doubleToString(sig->offset) << ") ["
                  << utils::doubleToString(sig->min) << "|" << utils::doubleToString(sig->max) << "] \"" << sig->unit
                  << "\" " << (sig->receiver_name.isEmpty() ? DEFAULT_NODE_NAME : sig->receiver_name) << "\n";

      if (!sig->comment.isEmpty())
        comm_stream << "CM_ SG_ " << address << " " << sig->name << " \"" << quoteEscape(sig->comment) << "\";\n";

      if (!sig->value_table.empty()) {
        val_stream << "VAL_ " << address << " " << sig->name;
        for (const auto& [val, desc] : sig->value_table)
          val_stream << " " << static_cast<long long>(val) << " \"" << desc << "\"";
        val_stream << ";\n";
      }
    }
    body_stream << "\n";
  }

  return header + body + comments + value_tables;
}

}  // namespace dbc
