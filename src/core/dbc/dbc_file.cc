#include "dbc_file.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include "utils/util.h"

namespace dbc {

static const QRegularExpression RE_SIGNAL(
    R"(^SG_\s+(?<name>\w+)\s*(?<mux>M|m\d+)?\s*:\s*(?<start>\d+)\|(?<size>\d+)@(?<endian>[01])(?<sign>[\+-])\s*\((?<factor>[0-9.+\-eE]+),(?<offset>[0-9.+\-eE]+)\)\s*\[(?<min>[0-9.+\-eE]+)\|(?<max>[0-9.+\-eE]+)\]\s*\"(?<unit>.*)\"\s*(?<receiver>.*))");
static const QRegularExpression RE_MESSAGE(R"(^BO_ (?<address>\w+) (?<name>\w+) *: (?<size>\w+) (?<transmitter>\w+))");
static const QRegularExpression RE_GENERAL_COMMENT(R"(CM_\s*\"(.*)\"\s*;)",
                                                   QRegularExpression::DotMatchesEverythingOption);
static const QRegularExpression RE_COMMENT(R"(CM_\s+(BO_|SG_)\s+(\d+)\s*(\w+)?\s*\"(.*)\"\s*;)",
                                           QRegularExpression::DotMatchesEverythingOption);
static const QRegularExpression RE_NODE_COMMENT(R"(CM_\s+BU_\s+(\w+)\s*\"(.*)\"\s*;)",
                                                QRegularExpression::DotMatchesEverythingOption);
static const QRegularExpression RE_VALUE_HEADER(R"(VAL_\s+(\d+)\s+(\w+))");
static const QRegularExpression RE_VALUE_PAIR(R"((-?\d+)\s+\"([^\"]*)\")");
static const QRegularExpression RE_BA_DEF(
    R"(BA_DEF_\s+(?:(BU_|BO_|SG_)\s+)?\"([^\"]+)\"\s+(INT|HEX|FLOAT|STRING|ENUM)\s*(.*);)");
static const QRegularExpression RE_BA_DEF_DEF(R"(BA_DEF_DEF_\s+\"([^\"]+)\"\s+(.*);)");
static const QRegularExpression RE_BA(R"(BA_\s+\"([^\"]+)\"\s+(.*);)");
static const QRegularExpression RE_VAL_TABLE(R"(VAL_TABLE_\s+(\w+)\s+(.*);)");
static const QRegularExpression RE_BO_TX_BU(R"(BO_TX_BU_\s+(\d+)\s*:\s*(.*);)");
static const QRegularExpression RE_SIG_VALTYPE(R"(SIG_VALTYPE_\s+(\d+)\s+(\w+)\s*:\s*(\d+)\s*;)");

// Helper: read until semicolon terminator
static QString readUntilSemicolon(const QString& first_line, QTextStream& stream) {
  QString raw = first_line;
  while (!raw.contains(';') && !stream.atEnd()) {
    raw += " " + stream.readLine().trimmed();
  }
  return raw;
}
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
  return safeToFile(filename);
}

bool File::saveAs(const QString& new_filename) {
  filename = new_filename;
  return safeToFile(filename);
}

bool File::safeToFile(const QString& fn) {
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
  return m ? m->findSignal(name) : nullptr;
}

void File::parse(const QString& content) {
  msgs.clear();
  general_comments.clear();
  nodes.clear();
  node_comments.clear();
  attribute_definitions.clear();
  attribute_values.clear();
  val_tables.clear();
  tx_nodes.clear();
  tail_lines.clear();

  int line_num = 0;
  QString line;
  dbc::Msg* current_msg = nullptr;
  int multiplexor_cnt = 0;
  bool seen_first = false;
  QTextStream stream((QString*)&content);

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
        parseVAL(line, stream);
      } else if (line.startsWith("CM_ BO_") || line.startsWith("CM_ SG_ ")) {
        parseComment(line, stream);
      } else if (line.startsWith("CM_ BU_ ")) {
        QString raw = line;
        while (!raw.endsWith(';') && !stream.atEnd()) raw += "\n" + stream.readLine();
        auto match = RE_NODE_COMMENT.match(raw);
        if (match.hasMatch()) {
          node_comments[match.captured(1)] = match.captured(2).replace("\\\"", "\"").trimmed();
        }
      } else if (line.startsWith("CM_ ")) {
        parseGeneralComment(line, stream);
      } else if (line.startsWith("BU_:") || line.startsWith("BU_ :")) {
        parseBU(line);
      } else if (line.startsWith("BA_DEF_DEF_ ")) {
        parseBA_DEF_DEF(readUntilSemicolon(line, stream));
      } else if (line.startsWith("BA_DEF_ ")) {
        parseBA_DEF(readUntilSemicolon(line, stream));
      } else if (line.startsWith("BA_ ")) {
        parseBA(readUntilSemicolon(line, stream));
      } else if (line.startsWith("VAL_TABLE_ ")) {
        parseVAL_TABLE(readUntilSemicolon(line, stream));
      } else if (line.startsWith("BO_TX_BU_ ")) {
        parseBO_TX_BU(readUntilSemicolon(line, stream));
      } else if (line.startsWith("SIG_VALTYPE_ ")) {
        parseSIG_VALTYPE(readUntilSemicolon(line, stream));
      } else {
        seen = false;
      }
    } catch (std::exception& e) {
      throw std::runtime_error(
          QString("[%1:%2]%3: %4").arg(filename).arg(line_num).arg(e.what()).arg(line).toStdString());
    }

    if (seen) {
      seen_first = true;
    } else if (!seen_first) {
      header += raw_line + "\n";
    } else if (!line.isEmpty()) {
      // Preserve unrecognized lines after first recognized keyword for round-trip fidelity
      tail_lines.push_back(raw_line);
    }
  }

  for (auto& [_, m] : msgs) {
    m.update();
  }
}

dbc::Msg* File::parseBO(const QString& line) {
  auto match = RE_MESSAGE.match(line);
  if (!match.hasMatch()) throw std::runtime_error("Invalid BO_ line format");

  uint32_t address = match.captured("address").toUInt();

  // Bit 31 indicates extended CAN frame format; mask it off for the actual address
  address &= ~0x80000000U;

  if (msgs.count(address) > 0)
    throw std::runtime_error(QString("Duplicate message address: %1").arg(address).toStdString());

  // Create a new message object
  dbc::Msg* msg = &msgs[address];
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
  if (current_msg->findSignal(name) != nullptr) {
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
  s.start_bit = match.captured("start").toInt();
  s.size = match.captured("size").toInt();
  s.is_little_endian = (match.captured("endian") == "1");
  s.is_signed = (match.captured("sign") == "-");

  if (s.size < 1 || s.size > 64) {
    throw std::runtime_error(QString("Signal '%1' has invalid size: %2").arg(name).arg(s.size).toStdString());
  }

  // Physical range and Factor
  s.factor = match.captured("factor").toDouble();
  s.offset = match.captured("offset").toDouble();
  s.min = match.captured("min").toDouble();
  s.max = match.captured("max").toDouble();

  // Metadata
  s.unit = match.captured("unit");
  s.receiver_name = match.captured("receiver").trimmed();

  current_msg->addSignal(s);
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

  // CM_ SG_ without signal name is treated as a message comment
  if (match.captured(1) == "BO_" || (match.captured(1) == "SG_" && match.captured(3).isEmpty())) {
    if (auto m = msg(addr)) m->comment = comment;
  } else {
    if (auto s = signal(addr, match.captured(3))) s->comment = comment;
  }
}

void File::parseVAL(const QString& line, QTextStream& stream) {
  // Accumulate multi-line VAL_ entries until ';' terminator
  QString raw = line;
  while (!raw.contains(';') && !stream.atEnd()) {
    raw += " " + stream.readLine().trimmed();
  }

  auto header_match = RE_VALUE_HEADER.match(raw);
  if (!header_match.hasMatch()) return;

  uint32_t addr = header_match.captured(1).toUInt();
  QString sig_name = header_match.captured(2);

  if (auto s = signal(addr, sig_name)) {
    s->value_table.clear();

    auto it = RE_VALUE_PAIR.globalMatch(raw);
    while (it.hasNext()) {
      auto match = it.next();
      s->value_table.push_back({match.captured(1).toDouble(), match.captured(2)});
    }
  }
}

void File::parseGeneralComment(const QString& line, QTextStream& stream) {
  QString raw = line;
  while (!raw.endsWith(';') && !stream.atEnd()) {
    raw += "\n" + stream.readLine();
  }

  auto match = RE_GENERAL_COMMENT.match(raw);
  if (match.hasMatch()) {
    general_comments.push_back(match.captured(1).replace("\\\"", "\"").trimmed());
  }
}

void File::parseBU(const QString& line) {
  // BU_: Node1 Node2 Node3
  int colon = line.indexOf(':');
  if (colon < 0) return;
  QString rest = line.mid(colon + 1).trimmed();
  if (!rest.isEmpty()) {
    nodes = rest.split(QRegularExpression(R"(\s+)"), Qt::SkipEmptyParts);
  }
}

void File::parseBA_DEF(const QString& line) {
  auto match = RE_BA_DEF.match(line);
  if (!match.hasMatch()) return;

  AttributeDef def;
  QString scope_str = match.captured(1);
  if (scope_str == "BU_") def.scope = AttributeDef::Scope::Node;
  else if (scope_str == "BO_") def.scope = AttributeDef::Scope::Message;
  else if (scope_str == "SG_") def.scope = AttributeDef::Scope::Signal;
  else def.scope = AttributeDef::Scope::Global;

  def.name = match.captured(2);
  QString type_str = match.captured(3);
  QString rest = match.captured(4).trimmed();

  if (type_str == "INT") {
    def.value_type = AttributeDef::ValueType::Int;
    auto parts = rest.split(' ', Qt::SkipEmptyParts);
    if (parts.size() >= 2) { def.min = parts[0].toDouble(); def.max = parts[1].toDouble(); }
  } else if (type_str == "HEX") {
    def.value_type = AttributeDef::ValueType::Hex;
    auto parts = rest.split(' ', Qt::SkipEmptyParts);
    if (parts.size() >= 2) { def.min = parts[0].toDouble(); def.max = parts[1].toDouble(); }
  } else if (type_str == "FLOAT") {
    def.value_type = AttributeDef::ValueType::Float;
    auto parts = rest.split(' ', Qt::SkipEmptyParts);
    if (parts.size() >= 2) { def.min = parts[0].toDouble(); def.max = parts[1].toDouble(); }
  } else if (type_str == "STRING") {
    def.value_type = AttributeDef::ValueType::String;
  } else if (type_str == "ENUM") {
    def.value_type = AttributeDef::ValueType::Enum;
    // Parse comma-separated quoted values: "val1","val2",...
    static const QRegularExpression re_enum_val(R"(\"([^\"]*)\")");
    auto it = re_enum_val.globalMatch(rest);
    while (it.hasNext()) {
      def.enum_values.append(it.next().captured(1));
    }
  }

  attribute_definitions.push_back(std::move(def));
}

void File::parseBA_DEF_DEF(const QString& line) {
  auto match = RE_BA_DEF_DEF.match(line);
  if (!match.hasMatch()) return;

  QString attr_name = match.captured(1);
  QString value = match.captured(2).trimmed();
  // Strip surrounding quotes if present
  if (value.startsWith('"') && value.endsWith('"')) {
    value = value.mid(1, value.size() - 2);
  }

  for (auto& def : attribute_definitions) {
    if (def.name == attr_name) {
      def.default_value = value;
      break;
    }
  }
}

void File::parseBA(const QString& line) {
  auto match = RE_BA.match(line);
  if (!match.hasMatch()) return;

  AttributeValue av;
  av.attr_name = match.captured(1);
  QString rest = match.captured(2).trimmed();

  if (rest.startsWith("BU_ ")) {
    av.scope = AttributeDef::Scope::Node;
    rest = rest.mid(4).trimmed();
    int sp = rest.indexOf(' ');
    if (sp > 0) {
      av.node_name = rest.left(sp);
      av.value = rest.mid(sp + 1).trimmed();
    }
  } else if (rest.startsWith("BO_ ")) {
    av.scope = AttributeDef::Scope::Message;
    rest = rest.mid(4).trimmed();
    int sp = rest.indexOf(' ');
    if (sp > 0) {
      av.address = rest.left(sp).toUInt();
      av.value = rest.mid(sp + 1).trimmed();
    }
  } else if (rest.startsWith("SG_ ")) {
    av.scope = AttributeDef::Scope::Signal;
    rest = rest.mid(4).trimmed();
    // SG_ <addr> <sig_name> <value>
    auto parts = rest.split(' ', Qt::SkipEmptyParts);
    if (parts.size() >= 3) {
      av.address = parts[0].toUInt();
      av.signal_name = parts[1];
      av.value = parts.mid(2).join(' ');
    }
  } else {
    av.scope = AttributeDef::Scope::Global;
    av.value = rest;
  }

  // Strip surrounding quotes from value
  if (av.value.startsWith('"') && av.value.endsWith('"')) {
    av.value = av.value.mid(1, av.value.size() - 2);
  }

  attribute_values.push_back(std::move(av));
}

void File::parseVAL_TABLE(const QString& line) {
  auto match = RE_VAL_TABLE.match(line);
  if (!match.hasMatch()) return;

  QString table_name = match.captured(1);
  QString pairs_str = match.captured(2);

  ValueTable table;
  auto it = RE_VALUE_PAIR.globalMatch(pairs_str);
  while (it.hasNext()) {
    auto m = it.next();
    table.push_back({m.captured(1).toDouble(), m.captured(2)});
  }
  val_tables[table_name] = std::move(table);
}

void File::parseBO_TX_BU(const QString& line) {
  auto match = RE_BO_TX_BU.match(line);
  if (!match.hasMatch()) return;

  uint32_t addr = match.captured(1).toUInt();
  QStringList transmitters = match.captured(2).split(',', Qt::SkipEmptyParts);
  for (auto& t : transmitters) t = t.trimmed();
  tx_nodes[addr] = transmitters;
}

void File::parseSIG_VALTYPE(const QString& line) {
  auto match = RE_SIG_VALTYPE.match(line);
  if (!match.hasMatch()) return;

  uint32_t addr = match.captured(1).toUInt();
  QString sig_name = match.captured(2);
  int vtype = match.captured(3).toInt();

  if (auto s = signal(addr, sig_name)) {
    if (vtype == 1) s->val_type = dbc::Signal::ValType::IEEEFloat;
    else if (vtype == 2) s->val_type = dbc::Signal::ValType::IEEEDouble;
  }
}

QString File::toDBCString() {
  QString bu_section, body, comments, ba_def_section, ba_def_def_section;
  QString ba_section, vt_section, val_section, sig_valtype_section, bo_tx_section, tail;

  QTextStream bu_s(&bu_section);
  QTextStream body_s(&body);
  QTextStream comm_s(&comments);
  QTextStream ba_def_s(&ba_def_section);
  QTextStream ba_def_def_s(&ba_def_def_section);
  QTextStream ba_s(&ba_section);
  QTextStream vt_s(&vt_section);
  QTextStream val_s(&val_section);
  QTextStream svt_s(&sig_valtype_section);
  QTextStream botx_s(&bo_tx_section);
  QTextStream tail_s(&tail);

  // BU_
  if (!nodes.isEmpty()) {
    bu_s << "BU_:";
    for (const auto& n : nodes) bu_s << " " << n;
    bu_s << "\n\n";
  }

  for (const auto& [address, m] : msgs) {
    const QString transmitter = m.transmitter.isEmpty() ? DEFAULT_NODE_NAME : m.transmitter;
    body_s << "BO_ " << address << " " << m.name << ": " << m.size << " " << transmitter << "\n";

    if (!m.comment.isEmpty()) {
      comm_s << "CM_ BO_ " << address << " \"" << QString(m.comment).replace("\"", "\\\"") << "\";\n";
    }

    for (auto sig : m.getSignals()) {
      QString mux;
      if (sig->type == dbc::Signal::Type::Multiplexor) {
        mux = "M ";
      } else if (sig->type == dbc::Signal::Type::Multiplexed) {
        mux = QString("m%1 ").arg(sig->multiplex_value);
      }

      body_s << " SG_ " << sig->name << " " << mux << ": " << sig->start_bit << "|" << sig->size << "@"
             << (sig->is_little_endian ? '1' : '0') << (sig->is_signed ? '-' : '+') << " ("
             << utils::doubleToString(sig->factor) << "," << utils::doubleToString(sig->offset) << ") ["
             << utils::doubleToString(sig->min) << "|" << utils::doubleToString(sig->max) << "] \"" << sig->unit
             << "\" " << (sig->receiver_name.isEmpty() ? DEFAULT_NODE_NAME : sig->receiver_name) << "\n";

      if (!sig->comment.isEmpty()) {
        comm_s << "CM_ SG_ " << address << " " << sig->name << " \"" << QString(sig->comment).replace("\"", "\\\"")
               << "\";\n";
      }

      if (!sig->value_table.empty()) {
        val_s << "VAL_ " << address << " " << sig->name;
        for (const auto& [val, desc] : sig->value_table) {
          val_s << " " << val << " \"" << desc << "\"";
        }
        val_s << ";\n";
      }

      if (sig->val_type != dbc::Signal::ValType::Integer) {
        svt_s << "SIG_VALTYPE_ " << address << " " << sig->name << " : "
              << static_cast<int>(sig->val_type) << ";\n";
      }
    }
    body_s << "\n";
  }

  // General file-level comments
  for (const auto& gc : general_comments) {
    comm_s << "CM_ \"" << QString(gc).replace("\"", "\\\"") << "\";\n";
  }

  // Node comments
  for (const auto& [node, comment] : node_comments) {
    comm_s << "CM_ BU_ " << node << " \"" << QString(comment).replace("\"", "\\\"") << "\";\n";
  }

  // BA_DEF_
  for (const auto& def : attribute_definitions) {
    ba_def_s << "BA_DEF_ ";
    switch (def.scope) {
      case AttributeDef::Scope::Node: ba_def_s << "BU_ "; break;
      case AttributeDef::Scope::Message: ba_def_s << "BO_ "; break;
      case AttributeDef::Scope::Signal: ba_def_s << "SG_ "; break;
      default: break;
    }
    ba_def_s << " \"" << def.name << "\" ";
    switch (def.value_type) {
      case AttributeDef::ValueType::Int:
        ba_def_s << "INT " << (long long)def.min << " " << (long long)def.max;
        break;
      case AttributeDef::ValueType::Hex:
        ba_def_s << "HEX " << (long long)def.min << " " << (long long)def.max;
        break;
      case AttributeDef::ValueType::Float:
        ba_def_s << "FLOAT " << utils::doubleToString(def.min) << " " << utils::doubleToString(def.max);
        break;
      case AttributeDef::ValueType::String:
        ba_def_s << "STRING ";
        break;
      case AttributeDef::ValueType::Enum:
        ba_def_s << "ENUM ";
        for (int i = 0; i < def.enum_values.size(); ++i) {
          if (i > 0) ba_def_s << ",";
          ba_def_s << "\"" << def.enum_values[i] << "\"";
        }
        break;
    }
    ba_def_s << ";\n";
  }

  // BA_DEF_DEF_
  for (const auto& def : attribute_definitions) {
    if (!def.default_value.isEmpty()) {
      ba_def_def_s << "BA_DEF_DEF_ \"" << def.name << "\" ";
      if (def.value_type == AttributeDef::ValueType::String || def.value_type == AttributeDef::ValueType::Enum) {
        ba_def_def_s << "\"" << def.default_value << "\"";
      } else {
        ba_def_def_s << def.default_value;
      }
      ba_def_def_s << ";\n";
    }
  }

  // BA_
  for (const auto& av : attribute_values) {
    ba_s << "BA_ \"" << av.attr_name << "\" ";
    switch (av.scope) {
      case AttributeDef::Scope::Node:
        ba_s << "BU_ " << av.node_name << " ";
        break;
      case AttributeDef::Scope::Message:
        ba_s << "BO_ " << av.address << " ";
        break;
      case AttributeDef::Scope::Signal:
        ba_s << "SG_ " << av.address << " " << av.signal_name << " ";
        break;
      default: break;
    }
    // Check if value needs quoting
    bool is_string = false;
    for (const auto& def : attribute_definitions) {
      if (def.name == av.attr_name) {
        is_string = (def.value_type == AttributeDef::ValueType::String || def.value_type == AttributeDef::ValueType::Enum);
        break;
      }
    }
    if (is_string) {
      ba_s << "\"" << av.value << "\"";
    } else {
      ba_s << av.value;
    }
    ba_s << ";\n";
  }

  // VAL_TABLE_
  for (const auto& [name, table] : val_tables) {
    vt_s << "VAL_TABLE_ " << name;
    for (const auto& [val, desc] : table) {
      vt_s << " " << val << " \"" << desc << "\"";
    }
    vt_s << ";\n";
  }

  // BO_TX_BU_
  for (const auto& [addr, transmitters] : tx_nodes) {
    botx_s << "BO_TX_BU_ " << addr << " : " << transmitters.join(",") << ";\n";
  }

  // Unrecognized lines passthrough
  for (const auto& tl : tail_lines) {
    tail_s << tl << "\n";
  }

  // Standard DBC order: header, BU_, BO_/SG_, CM_, BA_DEF_, BA_DEF_DEF_, BA_, VAL_TABLE_, VAL_, SIG_VALTYPE_, BO_TX_BU_, tail
  QString result = header + bu_section + body + comments;
  if (!ba_def_section.isEmpty()) result += "\n" + ba_def_section;
  if (!ba_def_def_section.isEmpty()) result += "\n" + ba_def_def_section;
  if (!ba_section.isEmpty()) result += "\n" + ba_section;
  if (!vt_section.isEmpty()) result += "\n" + vt_section;
  result += val_section;
  if (!sig_valtype_section.isEmpty()) result += "\n" + sig_valtype_section;
  if (!bo_tx_section.isEmpty()) result += "\n" + bo_tx_section;
  if (!tail.isEmpty()) result += "\n" + tail;
  return result;
}

}  // namespace dbc
