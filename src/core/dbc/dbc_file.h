#pragma once

#include <QTextStream>
#include <map>

#include "dbc_message.h"

namespace dbc {

// BA_DEF_ attribute definition
struct AttributeDef {
  enum class Scope { Global, Message, Signal, Node };
  enum class ValueType { Int, Hex, Float, String, Enum };

  Scope scope;
  QString name;
  ValueType value_type;
  double min = 0, max = 0;          // INT/HEX/FLOAT
  QStringList enum_values;           // ENUM
  QString default_value;             // BA_DEF_DEF_ value
};

// BA_ attribute value
struct AttributeValue {
  QString attr_name;
  AttributeDef::Scope scope;
  uint32_t address = 0;             // BO_/SG_ target message
  QString signal_name;              // SG_ target signal
  QString node_name;                // BU_ target node
  QString value;
};

class File {
 public:
  File(const QString& dbc_file_name);
  File(const QString& name, const QString& content);
  ~File() {}

  bool save();
  bool saveAs(const QString& new_filename);
  bool safeToFile(const QString& fn);
  QString toDBCString();

  void updateMsg(const MessageId& id, const QString& name, uint32_t size, const QString& node, const QString& comment);
  inline void removeMsg(const MessageId& id) { msgs.erase(id.address); }

  inline const std::map<uint32_t, dbc::Msg>& getMessages() const { return msgs; }
  dbc::Msg* msg(uint32_t address);
  dbc::Msg* msg(const QString& name);
  inline dbc::Msg* msg(const MessageId& id) { return msg(id.address); }
  dbc::Signal* signal(uint32_t address, const QString& name);

  inline QString name() const { return name_.isEmpty() ? "untitled" : name_; }
  inline bool isEmpty() const { return msgs.empty() && name_.isEmpty(); }

  QString filename;

  // Industry-standard DBC sections (read-write round-trip)
  QStringList nodes;                                          // BU_
  std::map<QString, QString> node_comments;                   // CM_ BU_
  std::vector<AttributeDef> attribute_definitions;            // BA_DEF_
  std::vector<AttributeValue> attribute_values;               // BA_
  std::map<QString, ValueTable> val_tables;                   // VAL_TABLE_
  std::map<uint32_t, QStringList> tx_nodes;                   // BO_TX_BU_

 private:
  void parse(const QString& content);
  dbc::Msg* parseBO(const QString& line);
  void parseSG(const QString& line, dbc::Msg* current_msg, int& multiplexor_cnt);
  void parseComment(const QString& line, QTextStream& stream);
  void parseGeneralComment(const QString& line, QTextStream& stream);
  void parseVAL(const QString& line, QTextStream& stream);
  void parseBU(const QString& line);
  void parseBA_DEF(const QString& line);
  void parseBA_DEF_DEF(const QString& line);
  void parseBA(const QString& line);
  void parseVAL_TABLE(const QString& line);
  void parseBO_TX_BU(const QString& line);
  void parseSIG_VALTYPE(const QString& line);

  QString header;
  std::map<uint32_t, dbc::Msg> msgs;
  std::vector<QString> general_comments;
  std::vector<QString> tail_lines;  // Unrecognized lines after first BO_ (passthrough)
  QString name_;
};

}  // namespace dbc
