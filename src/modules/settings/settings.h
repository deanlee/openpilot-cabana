#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#define LIGHT_THEME 1
#define DARK_THEME 2

class Settings : public QObject {
  Q_OBJECT

 public:
  enum DragDirection {
    MsbFirst,
    LsbFirst,
    AlwaysLE,
    AlwaysBE,
  };

  Settings();
  ~Settings();

  bool absolute_time = false;
  int fps = 10;
  int max_cached_minutes = 30;
  int chart_height = 200;
  int chart_column_count = 1;
  int chart_range = 3 * 60;  // 3 minutes
  int chart_series_type = 0;
  int theme = 0;
  int sparkline_range = 15;  // 15 seconds
  bool log_livestream = true;
  QString log_path;
  QString last_dir;
  QString last_route_dir;
  QByteArray geometry;
  QByteArray video_splitter_state;
  QByteArray window_state;
  QStringList recent_files;
  QByteArray message_header_state;
  DragDirection drag_direction = MsbFirst;

  // Last opened stream options and parameters
  int last_stream_option = 0;
  QString last_route;
  QStringList last_asc_files;
  QStringList last_candump_files;
  QStringList last_trc_files;
  QString last_panda_serial;
  QString last_socketcan_device;
  QString last_device_ip;
  int last_device_type = 1; // Default to ZMQ (1)

  // session data
  QString recent_dbc_file;
  QString active_msg_id;
  QStringList selected_msg_ids;
  QStringList active_charts;

 signals:
  void changed();
};

extern Settings settings;
