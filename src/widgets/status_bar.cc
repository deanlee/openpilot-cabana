#include "status_bar.h"

#include <chrono>
#include <unistd.h>

#include <QFontDatabase>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>

#include "modules/system/stream_manager.h"
#include "core/streams/abstract_stream.h"
#include "core/streams/device_stream.h"
#ifdef __linux__
#include <unistd.h>

#include <fstream>
#include <numeric>
#elif __APPLE__
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#endif
#include "modules/settings/settings.h"
#include "replay/include/util.h"

StatusBar::StatusBar(QWidget* parent) : QStatusBar(parent) {
  progress_bar_ = new QProgressBar(this);
  progress_bar_->setRange(0, 100);
  progress_bar_->setFixedSize({230, 16});
  progress_bar_->setTextVisible(true);
  progress_bar_->setVisible(false);

  // Left-aligned Hint
  QLabel* help_label = new QLabel(tr("For Help, Press F1"), this);
  addWidget(help_label);

  // Right-aligned Metrics
  status_label_ = new QLabel(this);
  cpu_label_ = new QLabel(this);
  mem_label_ = new QLabel(this);
  live_stats_label_ = new QLabel(this);

  QFont mono_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  mono_font.setPixelSize(12);

  progress_bar_->setFont(mono_font);
  status_label_->setFont(mono_font);
  cpu_label_->setFont(mono_font);
  mem_label_->setFont(mono_font);
  live_stats_label_->setFont(mono_font);

  // Add in order (Right to Left)
  addPermanentWidget(progress_bar_);
  addPermanentWidget(status_label_);
  addPermanentWidget(cpu_label_);
  addPermanentWidget(mem_label_);
  addPermanentWidget(live_stats_label_);

  setStyleSheet("QStatusBar::item { border: none; padding-left: 10px; }");

  connect(&StreamManager::instance(), &StreamManager::streamChanged, this, &StatusBar::monitorLiveStream);

  timer_ = new QTimer(this);
  connect(timer_, &QTimer::timeout, this, &StatusBar::updateMetrics);
  timer_->start(2000);
}

void StatusBar::monitorLiveStream() {
  // Only monitor remote device streams (not local logs/replays)
  if (StreamManager::instance().isLiveStream() && 
      dynamic_cast<DeviceStream*>(StreamManager::stream()) != nullptr) {
    connect(StreamManager::stream(), &AbstractStream::eventsMerged, this, [this](const MessageEventsMap& events_map) {
      int64_t total_events = 0;
      uint64_t total_size = 0;
      for (const auto& [id, events] : events_map) {
        total_events += events.size();
        for (const auto* e : events) {
          total_size += sizeof(CanEvent) + e->size;
        }
      }

      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration<double>(now - last_time_).count();
      if (elapsed >= 1.0) {
        // Calculate average interval (ms) over the last period
        int64_t event_delta = total_events - last_event_count_;
        if (event_delta > 0) {
          last_avg_interval_ = (elapsed * 1000.0) / event_delta;
        }

        // Update last minute counters
        last_minute_count_ = total_events - last_event_count_;
        last_minute_data_ = total_size - last_data_size_;

        // Reset counters for next interval
        last_event_count_ = total_events;
        last_data_size_ = total_size;
        last_time_ = now;
      }
    });
  }
}

void StatusBar::updateMetrics() {
  double cpu_percent = 0.0;
  double mem_mb = 0.0;

  // Update live stream stats if active
  if (StreamManager::instance().isLiveStream() && 
      dynamic_cast<DeviceStream*>(StreamManager::stream()) != nullptr) {
    // Calculate total events and data size
    int64_t total_event_count = 0;
    uint64_t total_data_size = 0;
    const auto& events_map = StreamManager::stream()->eventsMap();
    for (const auto& [id, events] : events_map) {
      total_event_count += events.size();
      for (const auto* e : events) {
        total_data_size += sizeof(CanEvent) + e->size;
      }
    }

    QString stats_text = QString("Avg: %1ms | Last 1s: %2 events (%3) | Total: %4 events (%5)")
                            .arg(last_avg_interval_, 6, 'f', 2)
                            .arg(last_minute_count_, 6)
                            .arg(QString::fromStdString(formattedDataSize(last_minute_data_)))
                            .arg(total_event_count, 6)
                            .arg(QString::fromStdString(formattedDataSize(total_data_size)));
    live_stats_label_->setText(stats_text);
  } else {
    live_stats_label_->setText("");
  }

#ifdef __linux__
  uint64_t proc_utime, proc_stime;
  std::ifstream proc_stat("/proc/self/stat");
  std::string dummy;
  for (int i = 0; i < 13; ++i) proc_stat >> dummy;
  proc_stat >> proc_utime >> proc_stime;
  uint64_t proc_total = proc_utime + proc_stime;

  std::ifstream sys_stat("/proc/stat");
  sys_stat >> dummy;
  uint64_t sys_total = 0, val;
  while (sys_stat >> val) sys_total += val;

  if (last_sys_time_ > 0 && sys_total > last_sys_time_) {
    cpu_percent =
        (100.0 * (proc_total - last_proc_time_) / (sys_total - last_sys_time_)) * sysconf(_SC_NPROCESSORS_ONLN);
  }
  last_proc_time_ = proc_total;
  last_sys_time_ = sys_total;

  std::ifstream stat_m("/proc/self/statm");
  long pages;
  if (stat_m >> pages >> pages) mem_mb = (pages * sysconf(_SC_PAGESIZE)) / 1024.0 / 1024.0;

#elif __APPLE__
  task_thread_times_info_data_t thread_info;
  mach_msg_type_number_t count = TASK_THREAD_TIMES_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_THREAD_TIMES_INFO, (task_info_t)&thread_info, &count) == KERN_SUCCESS) {
    uint64_t user_time = thread_info.user_time.seconds * 1000000 + thread_info.user_time.microseconds;
    uint64_t sys_time = thread_info.system_time.seconds * 1000000 + thread_info.system_time.microseconds;
    uint64_t total_proc_time = user_time + sys_time;

    if (last_proc_time_ > 0) {
      // macOS task_info provides absolute time; we divide by the timer interval (2s)
      cpu_percent = (double)(total_proc_time - last_proc_time_) / (2000000.0) * 100.0;
    }
    last_proc_time_ = total_proc_time;
  }

  struct mach_task_basic_info info;
  mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &infoCount) == KERN_SUCCESS) {
    mem_mb = info.resident_size / 1024.0 / 1024.0;
  }
#endif

  QString cpu_val = QString::number(cpu_percent, 'f', 1);
  cpu_label_->setText(tr("CPU:%1%").arg(cpu_val, 5));

  QString mem_val = QString::number(mem_mb, 'f', 0);
  mem_label_->setText(tr("MEM:%1 MB").arg(mem_val, 4));

  status_label_->setText(tr("Cache: %1m | FPS: %2").arg(settings.max_cached_minutes).arg(settings.fps));
}

void StatusBar::updateDownloadProgress(uint64_t cur, uint64_t total, bool success) {
  if (success && total > 0 && cur < total) {
    double p = (static_cast<double>(cur) / total) * 100.0;
    progress_bar_->setValue(static_cast<int>(p));

    // Show current size / total size in format
    QString size_str = QString::fromStdString(formattedDataSize(total));
    progress_bar_->setFormat(tr("Downloading %1 (%p%)").arg(size_str));

    if (!progress_bar_->isVisible()) progress_bar_->show();
  } else {
    progress_bar_->hide();
  }
}
