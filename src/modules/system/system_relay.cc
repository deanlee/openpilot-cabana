#include "system_relay.h"

#include <QDebug>

#include "core/dbc/dbc_manager.h"
#include "replay/include/http.h"
#include "replay/include/util.h"

SystemRelay& SystemRelay::instance() {
  static SystemRelay s;
  return s;
}

void SystemRelay::installGlobalHandlers() {
  static bool installed = false;
  if (installed) return;  // Prevent double-installation
  installed = true;

  qRegisterMetaType<uint64_t>("uint64_t");

  installDownloadProgressHandler([](uint64_t cur, uint64_t total, bool success) {
    emit SystemRelay::instance().downloadProgress(cur, total, success);
  });

  qInstallMessageHandler([](QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    if (type == QtDebugMsg) return;
    emit SystemRelay::instance().logMessage(msg);
  });

  installMessageHandler([](ReplyMsgType type, const std::string msg) {
    qInfo() << msg.c_str();
  });
}

void SystemRelay::uninstallHandlers() {
  qInstallMessageHandler(nullptr);

  installDownloadProgressHandler(nullptr);

  this->disconnect();
}
