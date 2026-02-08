#pragma once

#include <QObject>
#include <map>
#include <memory>
#include <set>

#include "dbc_file.h"

const int GLOBAL_SOURCE_ID = -1;

using SourceSet = std::set<int>;
const SourceSet SOURCE_ALL = {GLOBAL_SOURCE_ID};

inline bool operator<(const std::shared_ptr<dbc::File>& l, const std::shared_ptr<dbc::File>& r) {
  return l.get() < r.get();
}

namespace dbc {

class Manager : public QObject {
  Q_OBJECT

 public:
  Manager(QObject* parent);
  ~Manager() {}
  bool open(const SourceSet& sources, const QString& dbc_file_name, QString* error = nullptr);
  bool open(const SourceSet& sources, const QString& name, const QString& content, QString* error = nullptr);
  void close(const SourceSet& sources);
  void close(File* dbc_file);
  void closeAll();

  void addSignal(const MessageId& id, const dbc::Signal& sig);
  void updateSignal(const MessageId& id, const QString& sig_name, const dbc::Signal& sig);
  void removeSignal(const MessageId& id, const QString& sig_name);

  void updateMsg(const MessageId& id, const QString& name, uint32_t size, const QString& node, const QString& comment);
  void removeMsg(const MessageId& id);

  QString newMsgName(const MessageId& id);
  QString newSignalName(const MessageId& id);

  const std::map<uint32_t, dbc::Msg>& getMessages(uint8_t source = GLOBAL_SOURCE_ID);
  dbc::Msg* msg(const MessageId& id);
  dbc::Msg* msg(uint8_t source, const QString& name);

  QStringList signalNames();
  inline int dbcCount() { return allDBCFiles().size(); }
  int nonEmptyDBCCount();

  const SourceSet sources(const File* dbc_file) const;
  File* findDBCFile(const uint8_t source);
  inline File* findDBCFile(const MessageId& id) { return findDBCFile(id.source); }
  std::set<File*> allDBCFiles();

 signals:
  void signalAdded(MessageId id, const dbc::Signal* sig);
  void signalRemoved(const dbc::Signal* sig);
  void signalUpdated(const dbc::Signal* sig);
  void msgUpdated(MessageId id);
  void msgRemoved(MessageId id);
  void DBCFileChanged();
  void maskUpdated(const MessageId& address);

 private:
  std::map<int, std::shared_ptr<File>> dbc_files;
};

}  // namespace dbc

dbc::Manager* GetDBC();

QString toString(const SourceSet& ss);
inline QString msgName(const MessageId& id) {
  auto msg = GetDBC()->msg(id);
  return msg ? msg->name : UNDEFINED;
}
