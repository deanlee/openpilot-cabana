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
  QToolButton* createToolButton(const QString &icon, const QString &tip, std::function<void()> cb);
  void loopPlaybackClicked();
  void vipcAvailableStreamsUpdated(std::set<VisionStreamType> streams);
  void showRouteInfo();
  void onStreamChanged();

  QWidget *camera_widget = nullptr;
  QTabBar *camera_tab = nullptr;
  PlaybackCameraView *cam_widget;
  TimeLabel *time_label = nullptr;
  TimelineSlider *slider = nullptr;

// Toolbar Buttons
  QToolButton *play_toggle_btn = nullptr;
  QToolButton *speed_btn = nullptr;
  QToolButton *loop_btn = nullptr;
  QToolButton *skip_to_end_btn = nullptr;
  QToolButton *route_info_btn = nullptr;
};
