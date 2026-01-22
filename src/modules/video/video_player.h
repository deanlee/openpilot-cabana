#pragma once

#include <QFontDatabase>
#include <QFrame>
#include <QTabBar>
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

class ToolButton;

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
  void createSpeedDropdown();
  ToolButton* createToolButton(const QString &icon, const QString &tip, std::function<void()> cb);
  void loopPlaybackClicked();
  void vipcAvailableStreamsUpdated(std::set<VisionStreamType> streams);
  void showRouteInfo();
  void resetState();

  QWidget *camera_widget = nullptr;
  QTabBar *camera_tab = nullptr;
  PlaybackCameraView *cam_widget;
  TimeLabel *time_label = nullptr;
  TimelineSlider *slider = nullptr;

// Toolbar Buttons
  ToolButton *play_toggle_btn = nullptr;
  ToolButton *loop_btn = nullptr;
  ToolButton *skip_to_end_btn = nullptr;
  ToolButton *route_info_btn = nullptr;
  QToolButton *speed_btn = nullptr;
};
