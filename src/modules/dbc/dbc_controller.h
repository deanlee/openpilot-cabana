#pragma once

#include <QJsonDocument>
#include <QMenu>
#include <QObject>
#include <QString>

#include "core/dbc/dbc_manager.h"

namespace dbc {
class File;
}

class DbcController : public QObject {
  Q_OBJECT
 public:
  explicit DbcController(QWidget* parent = nullptr);

  void newFile(SourceSet s = SOURCE_ALL);
  void openFile(SourceSet s = SOURCE_ALL);
  void loadFile(const QString& fn, SourceSet s = SOURCE_ALL);
  void loadFromClipboard(SourceSet s = SOURCE_ALL, bool close_all = false);
  void loadFromOpendbc(const QString& name);
  void loadFromFingerprint(const QString& fingerprint, SourceSet s = SOURCE_ALL);

  void save();
  void saveAs();
  void saveFile(dbc::File* dbc_file);
  void saveFileAs(dbc::File* dbc_file);

  void saveToClipboard();
  void saveFileToClipboard(dbc::File* dbc_file);

  void closeFile(SourceSet s = SOURCE_ALL);
  void closeFile(dbc::File* dbc_file);

  void populateOpendbcFiles(QMenu* opendbc_menu);
  void populateRecentMenu(QMenu* recent_menu);
  void remindSaveChanges();
  void updateRecentFiles(const QString& fn);

 signals:
  void statusMessage(const QString& msg, int timeout_ms = 2000);

 private:
  QWidget* parent_ = nullptr;
  QJsonDocument fingerprint_to_dbc_;
};
