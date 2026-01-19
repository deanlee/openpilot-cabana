#pragma once

#include <QFontDatabase>
#include <QFrame>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "core/streams/replay_stream.h"
#include "playback_view.h"
#include "time_label.h"
#include "timeline_slider.h"
#include "utils/util.h"

class VideoPlayer : public QFrame {
  Q_OBJECT

public:
  VideoPlayer(QWidget *parnet = nullptr);
  void showThumbnail(double seconds);

protected:
  void setupConnections();
  QString formatTime(double sec, bool include_milliseconds = false);
  void timeRangeChanged();
  void updateState();
  void updatePlayBtnState();
  QWidget *createCameraWidget();
  void createPlaybackController();
  void createSpeedDropdown(QToolBar *toolbar);
  void loopPlaybackClicked();
  void vipcAvailableStreamsUpdated(std::set<VisionStreamType> streams);
  void showRouteInfo();
  void onStreamChanged();

  QWidget *camera_widget = nullptr;
  PlaybackCameraView *cam_widget;
  // QAction *time_display_action = nullptr;
  QAction *play_toggle_action = nullptr;
  QToolButton *speed_btn = nullptr;
  QAction *skip_to_end_action = nullptr;
  QAction *route_info_action = nullptr;
  QAction *loop_action = nullptr;
  TimelineSlider *slider = nullptr;
  TimeLabel *time_label = nullptr;
  QTabBar *camera_tab = nullptr;
};
