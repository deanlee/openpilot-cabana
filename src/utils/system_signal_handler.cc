#include "system_signal_handler.h"

#include <sys/socket.h>

#include <QApplication>
#include <csignal>

SystemSignalHandler::SystemSignalHandler(QObject* parent) : QObject(nullptr) {
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sig_fd)) {
    qFatal("Couldn't create TERM socketpair");
  }

  sn = new QSocketNotifier(sig_fd[1], QSocketNotifier::Read, this);
  connect(sn, &QSocketNotifier::activated, this, &SystemSignalHandler::handleSigTerm);
  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, SystemSignalHandler::signalHandler);
}

SystemSignalHandler::~SystemSignalHandler() {
  ::close(sig_fd[0]);
  ::close(sig_fd[1]);
}

void SystemSignalHandler::signalHandler(int s) { ::write(sig_fd[0], &s, sizeof(s)); }

void SystemSignalHandler::handleSigTerm() {
  sn->setEnabled(false);
  int tmp;
  ::read(sig_fd[1], &tmp, sizeof(tmp));

  printf("\nexiting...\n");
  qApp->closeAllWindows();
  qApp->exit();
}
