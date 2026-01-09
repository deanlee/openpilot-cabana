#pragma once

#include <QObject>

class SystemRelay : public QObject {
  Q_OBJECT
 public:
  static SystemRelay& instance();
  void installGlobalHandlers();
  void uninstallHandlers();

 signals:
  void downloadProgress(uint64_t cur, uint64_t total, bool success);
  void logMessage(const QString& msg, int timeout = 2000);
};
