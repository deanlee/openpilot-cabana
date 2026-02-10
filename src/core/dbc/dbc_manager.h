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
  ~Manager() = default;
  bool open(const SourceSet& sources, const QString& dbc_file_name, QString* error = nullptr);
  bool open(const SourceSet& sources, const QString& name, const QString& content, QString* error = nullptr);
  void closeSources(const SourceSet& sources);
  void closeFile(File* dbc_file);
  void closeAll();

  void addSignal(const MessageId& id, const dbc::Signal& sig);
  void updateSignal(const MessageId& id, const QString& sig_name, const dbc::Signal& sig);
  void removeSignal(const MessageId& id, const QString& sig_name);

  void updateMsg(const MessageId& id, const QString& name, uint32_t size, const QString& node, const QString& comment);
  void removeMsg(const MessageId& id);

  QString newMsgName(const MessageId& id) const;
  QString newSignalName(const MessageId& id) const;

  const std::map<uint32_t, dbc::Msg>& getMessages(uint8_t source = GLOBAL_SOURCE_ID) const;
  dbc::Msg* msg(const MessageId& id) const;
  dbc::Msg* msg(uint8_t source, const QString& name) const;

  QStringList signalNames() const;
  inline size_t fileCount() const { return unique_files.size(); }
  int nonEmptyFileCount() const;

  const SourceSet getSourcesForFile(const File* dbc_file) const;
  File* findDBCFile(const uint8_t source) const;
  const std::vector<std::shared_ptr<File>>& allFiles() const { return unique_files; };

 signals:
  void signalAdded(MessageId id, const dbc::Signal* sig);
  void signalRemoved(const dbc::Signal* sig);
  void signalUpdated(const dbc::Signal* sig);
  void msgUpdated(MessageId id);
  void msgRemoved(MessageId id);
  void DBCFileChanged();
  void maskUpdated(const MessageId& address);

 private:
  std::map<int, std::shared_ptr<File>> source_to_file;  // Source -> File (Fast Lookup)
  std::vector<std::shared_ptr<File>> unique_files;      // Unique List (UI & Lifecycle)

  void removeOrphanedFiles();
};

}  // namespace dbc

dbc::Manager* GetDBC();

QString toString(const SourceSet& ss);
inline QString msgName(const MessageId& id) {
  auto msg = GetDBC()->msg(id);
  return msg ? msg->name : UNDEFINED;
}
