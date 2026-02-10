#include "dbc_manager.h"

#include <QSet>
#include <algorithm>
#include <numeric>

namespace dbc {

Manager::Manager(QObject* parent) : QObject(parent) { qRegisterMetaType<SourceSet>("SourceSet"); }

bool Manager::open(const SourceSet& sources, const QString& dbc_file_name, QString* error) {
  try {
    auto it = std::ranges::find_if(dbc_files, [&](auto& f) { return f.second && f.second->filename == dbc_file_name; });
    auto file = (it != dbc_files.end()) ? it->second : std::make_shared<File>(dbc_file_name);
    for (auto s : sources) {
      dbc_files[s] = file;
    }
  } catch (std::exception& e) {
    if (error) *error = e.what();
    return false;
  }

  emit DBCFileChanged();
  return true;
}

bool Manager::open(const SourceSet& sources, const QString& name, const QString& content, QString* error) {
  try {
    auto file = std::make_shared<File>(name, content);
    for (auto s : sources) {
      dbc_files[s] = file;
    }
  } catch (std::exception& e) {
    if (error) *error = e.what();
    return false;
  }

  emit DBCFileChanged();
  return true;
}

void Manager::close(const SourceSet& sources) {
  for (auto s : sources) {
    dbc_files[s] = nullptr;
  }
  emit DBCFileChanged();
}

void Manager::close(File* dbc_file) {
  for (auto& [_, f] : dbc_files) {
    if (f.get() == dbc_file) f = nullptr;
  }
  emit DBCFileChanged();
}

void Manager::closeAll() {
  dbc_files.clear();
  emit DBCFileChanged();
}

void Manager::addSignal(const MessageId& id, const dbc::Signal& sig) {
  if (auto m = msg(id)) {
    if (auto s = m->addSignal(sig)) {
      emit signalAdded(id, s);
      emit maskUpdated(id);
    }
  }
}

void Manager::updateSignal(const MessageId& id, const QString& sig_name, const dbc::Signal& sig) {
  if (auto m = msg(id)) {
    if (auto s = m->updateSignal(sig_name, sig)) {
      emit signalUpdated(s);
      emit maskUpdated(id);
    }
  }
}

void Manager::removeSignal(const MessageId& id, const QString& sig_name) {
  if (auto m = msg(id)) {
    if (auto s = m->sig(sig_name)) {
      emit signalRemoved(s);
      m->removeSignal(sig_name);
      emit maskUpdated(id);
    }
  }
}

void Manager::updateMsg(const MessageId& id, const QString& name, uint32_t size, const QString& node,
                        const QString& comment) {
  auto dbc_file = findDBCFile(id.source);
  assert(dbc_file);  // This should be impossible
  dbc_file->updateMsg(id, name, size, node, comment);
  emit msgUpdated(id);
}

void Manager::removeMsg(const MessageId& id) {
  auto dbc_file = findDBCFile(id.source);
  assert(dbc_file);  // This should be impossible
  dbc_file->removeMsg(id);
  emit msgRemoved(id);
  emit maskUpdated(id);
}

QString Manager::newMsgName(const MessageId& id) {
  return QString("NEW_MSG_") + QString::number(id.address, 16).toUpper();
}

QString Manager::newSignalName(const MessageId& id) {
  auto m = msg(id);
  return m ? m->newSignalName() : "";
}

const std::map<uint32_t, dbc::Msg>& Manager::getMessages(uint8_t source) const {
  static std::map<uint32_t, dbc::Msg> empty_msgs;
  auto dbc_file = findDBCFile(source);
  return dbc_file ? dbc_file->getMessages() : empty_msgs;
}

dbc::Msg* Manager::msg(const MessageId& id) const {
  auto dbc_file = findDBCFile(id.source);
  return dbc_file ? dbc_file->msg(id) : nullptr;
}

dbc::Msg* Manager::msg(uint8_t source, const QString& name) const {
  auto dbc_file = findDBCFile(source);
  return dbc_file ? dbc_file->msg(name) : nullptr;
}

QStringList Manager::signalNames() {
  // Used for autocompletion
  QSet<QString> names;
  for (auto& f : allDBCFiles()) {
    for (auto& [_, m] : f->getMessages()) {
      for (auto sig : m.getSignals()) {
        names.insert(sig->name);
      }
    }
  }
  QStringList ret = names.values();
  ret.sort();
  return ret;
}

int Manager::nonEmptyDBCCount() {
  auto files = allDBCFiles();
  return std::ranges::count_if(files, [](auto& f) { return !f->isEmpty(); });
}

File* Manager::findDBCFile(const uint8_t source) const {
  // 1. Single lookup for the specific source
  if (auto it = dbc_files.find(source); it != dbc_files.end()) {
    return it->second.get();
  }

  // 2. Fallback lookup for GLOBAL_SOURCE_ID
  if (auto it = dbc_files.find(GLOBAL_SOURCE_ID); it != dbc_files.end()) {
    return it->second.get();
  }

  return nullptr;
}

std::set<File*> Manager::allDBCFiles() {
  std::set<File*> files;
  for (const auto& [_, f] : dbc_files) {
    if (f) files.insert(f.get());
  }
  return files;
}

const SourceSet Manager::sources(const File* dbc_file) const {
  SourceSet sources;
  for (auto& [s, f] : dbc_files) {
    if (f.get() == dbc_file) sources.insert(s);
  }
  return sources;
}

}  // namespace dbc

QString toString(const SourceSet& ss) {
  return std::accumulate(ss.cbegin(), ss.cend(), QString(), [](QString str, int source) {
    if (!str.isEmpty()) str += ", ";
    return str + (source == GLOBAL_SOURCE_ID ? QStringLiteral("all") : QString::number(source));
  });
}

dbc::Manager* GetDBC() {
  static dbc::Manager dbc_manager(nullptr);
  return &dbc_manager;
}
