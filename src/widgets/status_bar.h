#pragma once

#include <QStatusBar>

class QLabel;
class QProgressBar;
class QTimer;

class StatusBar : public QStatusBar {
  Q_OBJECT
 public:
  explicit StatusBar(QWidget* parent = nullptr);
  void updateDownloadProgress(uint64_t cur, uint64_t total, bool success);
  void updateMetrics();

 private:
  QProgressBar* progress_bar_;
  QLabel* status_label_;
  QLabel* cpu_label_;
  QLabel* mem_label_;
  QTimer* timer_;
  uint64_t last_proc_time_ = 0;
  uint64_t last_sys_time_ = 0;
};
