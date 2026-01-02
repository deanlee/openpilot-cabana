#include "settings.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <type_traits>

#include "utils/util.h"

Settings settings;

template <class SettingOperation>
void settings_op(SettingOperation op) {
  QSettings s("cabana");
  op(s, "absolute_time", settings.absolute_time);
  op(s, "fps", settings.fps);
  op(s, "max_cached_minutes", settings.max_cached_minutes);
  op(s, "chart_height", settings.chart_height);
  op(s, "chart_range", settings.chart_range);
  op(s, "chart_column_count", settings.chart_column_count);
  op(s, "last_dir", settings.last_dir);
  op(s, "last_route_dir", settings.last_route_dir);
  op(s, "window_state", settings.window_state);
  op(s, "geometry", settings.geometry);
  op(s, "video_splitter_state", settings.video_splitter_state);
  op(s, "recent_files", settings.recent_files);
  op(s, "message_header_state", settings.message_header_state);
  op(s, "chart_series_type", settings.chart_series_type);
  op(s, "theme", settings.theme);
  op(s, "sparkline_range", settings.sparkline_range);
  op(s, "multiple_lines_hex", settings.multiple_lines_hex);
  op(s, "log_livestream", settings.log_livestream);
  op(s, "log_path", settings.log_path);
  op(s, "drag_direction", (int &)settings.drag_direction);
  op(s, "suppress_defined_signals", settings.suppress_defined_signals);
  op(s, "recent_dbc_file", settings.recent_dbc_file);
  op(s, "active_msg_id", settings.active_msg_id);
  op(s, "selected_msg_ids", settings.selected_msg_ids);
  op(s, "active_charts", settings.active_charts);
}

Settings::Settings() {
  last_dir = last_route_dir = QDir::homePath();
  log_path = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/cabana_live_stream/";
  settings_op([](QSettings &s, const QString &key, auto &value) {
    if (auto v = s.value(key); v.canConvert<std::decay_t<decltype(value)>>())
      value = v.value<std::decay_t<decltype(value)>>();
  });
}

Settings::~Settings() {
  settings_op([](QSettings &s, const QString &key, auto &v) { s.setValue(key, v); });
}
