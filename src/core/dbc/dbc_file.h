#pragma once

#include <QTextStream>
#include <map>

#include "dbc_message.h"

namespace dbc {

class File {
 public:
  File(const QString& dbc_file_name);
  File(const QString& name, const QString& content);
  ~File() = default;

  bool save();
  bool saveAs(const QString& new_filename);
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

 private:
  void parse(QString content);
  bool saveToFile(const QString& fn);
  dbc::Msg* parseBO(const QString& line);
  void parseSG(const QString& line, dbc::Msg* current_msg, int& multiplexor_cnt);
  void parseComment(const QString& line, QTextStream& stream, int& line_num);
  void parseVAL(const QString& line);

  QString header;
  std::map<uint32_t, dbc::Msg> msgs;
  QString name_;
};

}  // namespace dbc
