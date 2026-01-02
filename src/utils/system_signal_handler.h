#pragma once
#include <QSocketNotifier>

class SystemSignalHandler : public QObject {
  Q_OBJECT

public:
  SystemSignalHandler(QObject *parent = nullptr);
  ~SystemSignalHandler();
  static void signalHandler(int s);

public slots:
  void handleSigTerm();

private:
  inline static int sig_fd[2] = {};
  QSocketNotifier *sn;
};
