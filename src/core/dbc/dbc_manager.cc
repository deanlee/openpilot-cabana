#include "dbc_manager.h"

#include <QSet>
#include <algorithm>
#include <numeric>

namespace dbc {

Manager::Manager(QObject* parent) : QObject(parent) { qRegisterMetaType<SourceSet>("SourceSet"); }

bool Manager::open(const SourceSet& sources, const QString& dbc_file_name, QString* error) {
  try {
    // 1. Find or Create the unique file
    auto it = std::ranges::find_if(unique_files, [&](const auto& f) { return f->filename == dbc_file_name; });

    std::shared_ptr<File> file = (it != unique_files.end()) ? *it : std::make_shared<File>(dbc_file_name);

    if (it == unique_files.end()) {
      unique_files.push_back(file);
    }

    // 2. Map the sources to this file
    for (int s : sources) {
      source_to_file[s] = file;
    }

    removeOrphanedFiles();
  } catch (const std::exception& e) {
    if (error) *error = e.what();
    return false;
  }

  emit DBCFileChanged();
  return true;
}

bool Manager::open(const SourceSet& sources, const QString& name, const QString& content, QString* error) {
  try {
    auto file = std::make_shared<File>(name, content);
    unique_files.push_back(file);
    for (int s : sources) {
      source_to_file[s] = file;
    }
    removeOrphanedFiles();
  } catch (const std::exception& e) {
    if (error) *error = e.what();
    return false;
  }

  emit DBCFileChanged();
  return true;
}

void Manager::closeSources(const SourceSet& sources) {
  for (int s : sources) {
    source_to_file.erase(s);
  }
  removeOrphanedFiles();
  emit DBCFileChanged();
}

void Manager::closeFile(File* dbc_file) {
  // Remove from map
  std::erase_if(source_to_file, [dbc_file](const auto& pair) { return pair.second.get() == dbc_file; });
  // Remove from unique list
  std::erase_if(unique_files, [dbc_file](const auto& f) { return f.get() == dbc_file; });

  emit DBCFileChanged();
}

void Manager::closeAll() {
  source_to_file.clear();
  unique_files.clear();
  emit DBCFileChanged();
}

void Manager::removeOrphanedFiles() {
  // Removes files from unique_files if no source is pointing to them anymore
  std::erase_if(unique_files, [this](const auto& file) {
    return std::ranges::none_of(source_to_file, [&](const auto& pair) { return pair.second == file; });
  });
}

// --- Signal & Message Logic ---

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
  if (auto dbc_file = findDBCFile(id.source)) {
    dbc_file->updateMsg(id, name, size, node, comment);
    emit msgUpdated(id);
  }
}

void Manager::removeMsg(const MessageId& id) {
  if (auto dbc_file = findDBCFile(id.source)) {
    dbc_file->removeMsg(id);
    emit msgRemoved(id);
    emit maskUpdated(id);
  }
}

QString Manager::newMsgName(const MessageId& id) const {
  return QString("NEW_MSG_") + QString::number(id.address, 16).toUpper();
}

QString Manager::newSignalName(const MessageId& id) const {
  auto m = msg(id);
  return m ? m->newSignalName() : "";
}

const std::map<uint32_t, dbc::Msg>& Manager::getMessages(uint8_t source) const {
  static const std::map<uint32_t, dbc::Msg> empty_msgs;
  auto f = findDBCFile(source);
  return f ? f->getMessages() : empty_msgs;
}

dbc::Msg* Manager::msg(const MessageId& id) const {
  auto f = findDBCFile(id.source);
  return f ? f->msg(id) : nullptr;
}

dbc::Msg* Manager::msg(uint8_t source, const QString& name) const {
  auto f = findDBCFile(source);
  return f ? f->msg(name) : nullptr;
}

QStringList Manager::signalNames() const {
  QSet<QString> names;
  for (const auto& file : unique_files) {
    for (const auto& [id, m] : file->getMessages()) {
      for (const auto* sig : m.getSignals()) {
        names.insert(sig->name);
      }
    }
  }
  QStringList ret = names.values();
  ret.sort(Qt::CaseInsensitive);
  return ret;
}

int Manager::nonEmptyFileCount() const {
  return static_cast<int>(std::ranges::count_if(unique_files, [](const auto& f) { return !f->isEmpty(); }));
}

File* Manager::findDBCFile(uint8_t source) const {
  if (auto it = source_to_file.find(source); it != source_to_file.end()) {
    return it->second.get();
  }
  if (auto it = source_to_file.find(GLOBAL_SOURCE_ID); it != source_to_file.end()) {
    return it->second.get();
  }
  return nullptr;
}

const SourceSet Manager::getSourcesForFile(const File* dbc_file) const {
  SourceSet result;
  for (const auto& [source, file_ptr] : source_to_file) {
    if (file_ptr.get() == dbc_file) result.insert(source);
  }
  return result;
}

}  // namespace dbc

QString toString(const SourceSet& ss) {
  return std::accumulate(ss.cbegin(), ss.cend(), QString(), [](QString str, int source) {
    if (!str.isEmpty()) str += QStringLiteral(", ");
    return str + (source == GLOBAL_SOURCE_ID ? QStringLiteral("all") : QString::number(source));
  });
}

dbc::Manager* GetDBC() {
  static dbc::Manager dbc_manager(nullptr);
  return &dbc_manager;
}
