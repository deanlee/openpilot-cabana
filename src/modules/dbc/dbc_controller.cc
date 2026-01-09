#include "dbc_controller.h"

#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonObject>
#include <QMessageBox>
#include <algorithm>

#include "core/commands/commands.h"
#include "core/dbc/dbc_manager.h"
#include "modules/settings/settings.h"

inline QString GetOpendbcFilePath(const QString& name) {
  return QDir::current().absoluteFilePath(QString("data/opendbc/%1").arg(name));
}

DbcController::DbcController(QWidget* parent)
    : QObject(parent), parent_(parent) {
  QFile json_file(GetOpendbcFilePath("car_fingerprint_to_dbc.json"));
  if (json_file.open(QIODevice::ReadOnly)) {
    fingerprint_to_dbc_ = QJsonDocument::fromJson(json_file.readAll());
  }
}

void DbcController::newFile(SourceSet s) {
  remindSaveChanges();
  GetDBC()->open(s, "", "");
}

void DbcController::openFile(SourceSet s) {
  remindSaveChanges();
  QString fn = QFileDialog::getOpenFileName(parent_, QObject::tr("Open File"), settings.last_dir, "DBC (*.dbc)");
  if (!fn.isEmpty()) loadFile(fn, s);
}

void DbcController::loadFile(const QString& fn, SourceSet s) {
  if (fn.isEmpty()) return;

  if (s == SOURCE_ALL) {
    // Close only existing DBCs for this file’s bus if known, otherwise leave as-is.
    // Callers may explicitly pass SourceSet for precise control.
  }
  QString error;
  if (GetDBC()->open(s, fn, &error)) {
    updateRecentFiles(fn);
    emit statusMessage(QObject::tr("DBC File %1 loaded").arg(fn));
  } else {
    QMessageBox msg_box(QMessageBox::Warning, QObject::tr("Failed to load DBC file"),
                        QObject::tr("Failed to parse DBC file %1").arg(fn));
    msg_box.setDetailedText(error);
    msg_box.exec();
  }
}

void DbcController::loadFromClipboard(SourceSet s, bool /*close_all*/) {
  remindSaveChanges();
  QString dbc_str = QGuiApplication::clipboard()->text();
  QString error;
  bool ret = GetDBC()->open(s, "", dbc_str, &error);
  if (ret && GetDBC()->nonEmptyDBCCount() > 0) {
    QMessageBox::information(parent_, QObject::tr("Load From Clipboard"),
                             QObject::tr("DBC Successfully Loaded!"));
  } else {
    QMessageBox msg_box(QMessageBox::Warning, QObject::tr("Failed to load DBC from clipboard"),
                        QObject::tr("Make sure that you paste the text with correct format."));
    msg_box.setDetailedText(error);
    msg_box.exec();
  }
}

void DbcController::loadFromOpendbc(const QString& name) {
  loadFile(GetOpendbcFilePath(name));
}

void DbcController::loadFromFingerprint(const QString& fingerprint, SourceSet s) {
  if (fingerprint.isEmpty() || !fingerprint_to_dbc_.object().contains(fingerprint)) {
    qWarning() << "Fingerprint not found in opendbc database:" << fingerprint;
    return;
  }
  QString dbc_name = fingerprint_to_dbc_[fingerprint].toString() + ".dbc";
  loadFromOpendbc(dbc_name);
}

void DbcController::save() {
  for (auto dbc_file : GetDBC()->allDBCFiles()) {
    if (!dbc_file->isEmpty()) saveFile(dbc_file);
  }
}

void DbcController::saveAs() {
  for (auto dbc_file : GetDBC()->allDBCFiles()) {
    if (!dbc_file->isEmpty()) saveFileAs(dbc_file);
  }
}

void DbcController::closeFile(SourceSet s) {
  remindSaveChanges();
  if (s == SOURCE_ALL)
    GetDBC()->closeAll();
  else
    GetDBC()->close(s);
}

void DbcController::closeFile(dbc::File* dbc_file) {
  Q_ASSERT(dbc_file != nullptr);
  remindSaveChanges();
  GetDBC()->close(dbc_file);
  if (GetDBC()->dbcCount() == 0) {
    newFile();
  }
}

void DbcController::saveFile(dbc::File* dbc_file) {
  Q_ASSERT(dbc_file != nullptr);
  if (!dbc_file->filename.isEmpty()) {
    dbc_file->save();
    UndoStack::instance()->setClean();
    emit statusMessage(QObject::tr("File saved"));
  } else if (!dbc_file->isEmpty()) {
    saveFileAs(dbc_file);
  }
}

void DbcController::saveFileAs(dbc::File* dbc_file) {
  QString title = QObject::tr("Save File (bus: %1)").arg(toString(GetDBC()->sources(dbc_file)));
  QString fn = QFileDialog::getSaveFileName(parent_, title,
                                            QDir::cleanPath(settings.last_dir + "/untitled.dbc"),
                                            QObject::tr("DBC (*.dbc)"));
  if (!fn.isEmpty()) {
    dbc_file->saveAs(fn);
    UndoStack::instance()->setClean();
    emit statusMessage(QObject::tr("File saved as %1").arg(fn));
    updateRecentFiles(fn);
  }
}

void DbcController::saveToClipboard() {
  for (auto dbc_file : GetDBC()->allDBCFiles()) {
    if (!dbc_file->isEmpty()) saveFileToClipboard(dbc_file);
  }
}

void DbcController::saveFileToClipboard(dbc::File* dbc_file) {
  Q_ASSERT(dbc_file != nullptr);
  QGuiApplication::clipboard()->setText(dbc_file->generateGetDBC());
  QMessageBox::information(parent_, QObject::tr("Copy To Clipboard"),
                           QObject::tr("DBC Successfully copied!"));
}

void DbcController::populateRecentMenu(QMenu* recent_menu) {
  recent_menu->clear();
  constexpr int MAX_RECENT_FILES = 10;
  int num_recent_files = std::min<int>(settings.recent_files.size(), MAX_RECENT_FILES);
  if (!num_recent_files) {
    recent_menu->addAction(QObject::tr("No Recent Files"))->setEnabled(false);
    return;
  }
  for (int i = 0; i < num_recent_files; ++i) {
    const QString file = settings.recent_files[i];
    QString text = QObject::tr("&%1 %2").arg(i + 1).arg(QFileInfo(file).fileName());
    recent_menu->addAction(text, this, [this, file]() { this->loadFile(file); });
  }
}

void DbcController::populateOpendbcFiles(QMenu* opendbc_menu) {
  opendbc_menu->clear();
  QString local_opendbc_path = GetOpendbcFilePath("");
  QDir opendbc_dir(local_opendbc_path);
  if (opendbc_dir.exists()) {
    for (const auto& dbc_name : opendbc_dir.entryList({"*.dbc"}, QDir::Files, QDir::Name)) {
      opendbc_menu->addAction(dbc_name, [this, dbc_name]() {
        loadFromOpendbc(dbc_name);
      });
    }
  } else {
    qWarning() << "opendbc folder not found at:" << local_opendbc_path;
  }
}

void DbcController::remindSaveChanges() {
  while (!UndoStack::instance()->isClean()) {
    QString text = QObject::tr("You have unsaved changes. Press ok to save them, cancel to discard.");
    int ret = QMessageBox::question(parent_, QObject::tr("Unsaved Changes"), text,
                                    QMessageBox::Ok | QMessageBox::Cancel);
    if (ret != QMessageBox::Ok) break;
    save();
  }
  UndoStack::instance()->clear();
}

void DbcController::updateRecentFiles(const QString& fn) {
  constexpr int MAX_RECENT_FILES = 10;
  settings.recent_files.removeAll(fn);
  settings.recent_files.prepend(fn);
  while (settings.recent_files.size() > MAX_RECENT_FILES) {
    settings.recent_files.removeLast();
  }
  settings.last_dir = QFileInfo(fn).absolutePath();
}
