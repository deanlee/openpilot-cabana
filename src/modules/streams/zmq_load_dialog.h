#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QTimer;
class ZmqDiagnosticWorker;

class ZmqLoadDialog : public QDialog {
 public:
  explicit ZmqLoadDialog(const QString& ip_address, QWidget* parent = nullptr);
  ~ZmqLoadDialog() override;

 private:
  void runDiagnostics();
  void updateUI();

  QString ip_;
  QLabel* title_label_ = nullptr;
  QLabel* ping_label_ = nullptr;
  QLabel* socket_label_ = nullptr;
  QLabel* zmq_label_ = nullptr;
  QLabel* ignition_label_ = nullptr;
  QLabel* advice_label_ = nullptr;
  QTimer* diag_timer_ = nullptr;
  ZmqDiagnosticWorker* worker_ = nullptr;
};
