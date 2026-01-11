#include "video_player.h"

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QMouseEvent>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

#include "modules/settings/settings.h"
#include "modules/system/stream_manager.h"
#include "tools/routeinfo.h"
#include "widgets/common.h"

static Replay *getReplay() {
  auto stream = qobject_cast<ReplayStream *>(StreamManager::stream());
  return stream ? stream->getReplay() : nullptr;
}

VideoPlayer::VideoPlayer(QWidget *parent) : QFrame(parent) {
  setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  auto main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(0);

  camera_widget = createCameraWidget();
  camera_widget->setVisible(false);
  main_layout->addWidget(camera_widget);

  createPlaybackController();

  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

  connect(&StreamManager::instance(), &StreamManager::streamChanged, this, &VideoPlayer::onStreamChanged);
  connect(&StreamManager::instance(), &StreamManager::paused, this, &VideoPlayer::updatePlayBtnState);
  connect(&StreamManager::instance(), &StreamManager::resume, this, &VideoPlayer::updatePlayBtnState);
  connect(&StreamManager::instance(), &StreamManager::snapshotsUpdated, this, &VideoPlayer::updateState);
  connect(&StreamManager::instance(), &StreamManager::seeking, this, &VideoPlayer::updateState);
  connect(&StreamManager::instance(), &StreamManager::timeRangeChanged, this, &VideoPlayer::timeRangeChanged);

  updatePlayBtnState();
  setWhatsThis(tr(R"(
    <b>Video</b><br />
    <!-- TODO: add descprition here -->
    <span style="color:gray">Timeline color</span>
    <table>
    <tr><td><span style="color:%1;">■ </span>Disengaged </td>
        <td><span style="color:%2;">■ </span>Engaged</td></tr>
    <tr><td><span style="color:%3;">■ </span>User Flag </td>
        <td><span style="color:%4;">■ </span>Info</td></tr>
    <tr><td><span style="color:%5;">■ </span>Warning </td>
        <td><span style="color:%6;">■ </span>Critical</td></tr>
    </table>
    <span style="color:gray">Shortcuts</span><br/>
    Pause/Resume: <span style="background-color:lightGray;color:gray">&nbsp;space&nbsp;</span>
  )").arg(timeline_colors[(int)TimelineType::None].name(),
          timeline_colors[(int)TimelineType::Engaged].name(),
          timeline_colors[(int)TimelineType::UserBookmark].name(),
          timeline_colors[(int)TimelineType::AlertInfo].name(),
          timeline_colors[(int)TimelineType::AlertWarning].name(),
          timeline_colors[(int)TimelineType::AlertCritical].name()));
}

void VideoPlayer::createPlaybackController() {
  QToolBar *toolbar = new QToolBar(this);
  layout()->addWidget(toolbar);

  int icon_size = style()->pixelMetric(QStyle::PM_SmallIconSize);
  toolbar->setIconSize({icon_size, icon_size});

  toolbar->addAction(utils::icon("step-back"), tr("Seek backward"), []() { StreamManager::stream()->seekTo(StreamManager::stream()->currentSec() - 1); });
  play_toggle_action = toolbar->addAction(utils::icon("play"), tr("Play"), []() { StreamManager::stream()->pause(!StreamManager::stream()->isPaused()); });
  toolbar->addAction(utils::icon("step-forward"), tr("Seek forward"), []() { StreamManager::stream()->seekTo(StreamManager::stream()->currentSec() + 1); });


  skip_to_end_action = toolbar->addAction(utils::icon("skip-forward"), tr("Skip to the end"), this, [this]() {
    // set speed to 1.0
    speed_btn->menu()->actions()[7]->setChecked(true);
    StreamManager::stream()->pause(false);
    StreamManager::stream()->seekTo(StreamManager::stream()->maxSeconds() + 1);
  });

  time_display_action = toolbar->addAction("", this, [this]() {
    settings.absolute_time = !settings.absolute_time;
    time_display_action->setToolTip(settings.absolute_time ? tr("Elapsed time") : tr("Absolute time"));
    updateState();
  });

  QWidget *spacer = new QWidget();
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  toolbar->addWidget(spacer);

  loop_action = toolbar->addAction(utils::icon("repeat"), tr("Loop playback"), this, &VideoPlayer::loopPlaybackClicked);
  createSpeedDropdown(toolbar);
  toolbar->addSeparator();
  route_info_action = toolbar->addAction(utils::icon("info"), tr("View route details"), this, &VideoPlayer::showRouteInfo);
  // hide repeat,separator info if living?
}

void VideoPlayer::createSpeedDropdown(QToolBar *toolbar) {
  toolbar->addWidget(speed_btn = new QToolButton(this));
  speed_btn->setMenu(new QMenu(speed_btn));
  speed_btn->setPopupMode(QToolButton::InstantPopup);
  QActionGroup *speed_group = new QActionGroup(this);
  speed_group->setExclusive(true);

  for (float speed : {0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 0.8, 1., 2., 3., 5.}) {
    auto act = speed_btn->menu()->addAction(QString("%1x").arg(speed), this, [this, speed]() {
      StreamManager::stream()->setSpeed(speed);
      speed_btn->setText(QString("%1x  ").arg(speed));
    });

    speed_group->addAction(act);
    act->setCheckable(true);
    if (speed == 1.0) {
      act->setChecked(true);
      act->trigger();
    }
  }

  QFont font = speed_btn->font();
  font.setBold(true);
  speed_btn->setFont(font);
  speed_btn->setMinimumWidth(speed_btn->fontMetrics().horizontalAdvance("0.05x  ") + style()->pixelMetric(QStyle::PM_MenuButtonIndicator));
}

QWidget *VideoPlayer::createCameraWidget() {
  QWidget *w = new QWidget(this);
  QVBoxLayout *l = new QVBoxLayout(w);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(0);

  l->addWidget(camera_tab = new TabBar(w));
  camera_tab->setAutoHide(true);
  camera_tab->setExpanding(false);

  l->addWidget(cam_widget = new PlaybackCameraView("camerad", VISION_STREAM_ROAD));
  cam_widget->setMinimumHeight(MIN_VIDEO_HEIGHT);
  cam_widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);

  l->addWidget(slider = new Slider(w));
  slider->setSingleStep(0);
  slider->setTimeRange(StreamManager::stream()->minSeconds(), StreamManager::stream()->maxSeconds());

  connect(slider, &QSlider::sliderReleased, [this]() { StreamManager::stream()->seekTo(slider->currentSecond()); });
  connect(&StreamManager::instance(), &StreamManager::paused, cam_widget, [c = cam_widget]() { c->showPausedOverlay(); });
  connect(&StreamManager::instance(), &StreamManager::eventsMerged, this, [this]() { slider->update(); });
  connect(&StreamManager::instance(), &StreamManager::qLogLoaded, cam_widget, &PlaybackCameraView::parseQLog, Qt::QueuedConnection);
  connect(cam_widget, &PlaybackCameraView::clicked, []() { StreamManager::stream()->pause(!StreamManager::stream()->isPaused()); });
  connect(cam_widget, &PlaybackCameraView::vipcAvailableStreamsUpdated, this, &VideoPlayer::vipcAvailableStreamsUpdated);
  connect(camera_tab, &QTabBar::currentChanged, [this](int index) {
    if (index != -1) cam_widget->setStreamType((VisionStreamType)camera_tab->tabData(index).toInt());
  });
  slider->installEventFilter(this);
  return w;
}

void VideoPlayer::onStreamChanged() {
  timeRangeChanged();
  updateState();
  updatePlayBtnState();
  bool is_live = StreamManager::stream()->liveStreaming();

  camera_widget->setVisible(!is_live);
  loop_action->setVisible(!is_live);
  route_info_action->setVisible(!is_live);
  skip_to_end_action->setVisible(is_live);
}

void VideoPlayer::vipcAvailableStreamsUpdated(std::set<VisionStreamType> streams) {
  static const QString stream_names[] = {"Road camera", "Driver camera", "Wide road camera"};
  for (int i = 0; i < streams.size(); ++i) {
    if (camera_tab->count() <= i) {
      camera_tab->addTab(QString());
    }
    int type = *std::next(streams.begin(), i);
    camera_tab->setTabText(i, stream_names[type]);
    camera_tab->setTabData(i, type);
  }
  while (camera_tab->count() > streams.size()) {
    camera_tab->removeTab(camera_tab->count() - 1);
  }
}

void VideoPlayer::loopPlaybackClicked() {
  bool is_looping = getReplay()->loop();
  getReplay()->setLoop(!is_looping);
  loop_action->setIcon(utils::icon(!is_looping ? "repeat" : "repeat-1"));
}

void VideoPlayer::timeRangeChanged() {
  const auto time_range = StreamManager::stream()->timeRange();
  if (StreamManager::stream()->liveStreaming()) {
    skip_to_end_action->setEnabled(!time_range.has_value());
    return;
  }
  time_range ? slider->setTimeRange(time_range->first, time_range->second)
             : slider->setTimeRange(StreamManager::stream()->minSeconds(), StreamManager::stream()->maxSeconds());
  updateState();
}

QString VideoPlayer::formatTime(double sec, bool include_milliseconds) {
  if (settings.absolute_time)
    sec = StreamManager::stream()->beginDateTime().addMSecs(sec * 1000).toMSecsSinceEpoch() / 1000.0;
  return utils::formatSeconds(sec, include_milliseconds, settings.absolute_time);
}

void VideoPlayer::updateState() {
  if (slider) {
    if (!slider->isSliderDown()) {
      slider->setCurrentSecond(StreamManager::stream()->currentSec());
    }
    if (camera_tab->count() == 0) {  //  No streams available
      cam_widget->update();          // Manually refresh to show alert events
    }
    time_display_action->setText(QString("%1 / %2").arg(formatTime(StreamManager::stream()->currentSec(), true),
                                             formatTime(slider->maximum() / slider->factor)));
  } else {
    time_display_action->setText(formatTime(StreamManager::stream()->currentSec(), true));
  }
}

void VideoPlayer::updatePlayBtnState() {
  play_toggle_action->setIcon(utils::icon(StreamManager::stream()->isPaused() ? "play" : "pause"));
  play_toggle_action->setToolTip(StreamManager::stream()->isPaused() ? tr("Play") : tr("Pause"));
}

void VideoPlayer::showThumbnail(double seconds) {
  if (StreamManager::stream()->liveStreaming()) return;

  cam_widget->thumbnail_dispaly_time = seconds;
  slider->thumbnail_dispaly_time = seconds;
  cam_widget->update();
  slider->update();
}

void VideoPlayer::showRouteInfo() {
  RouteInfoDlg *route_info = new RouteInfoDlg(this);
  route_info->setAttribute(Qt::WA_DeleteOnClose);
  route_info->show();
}

bool VideoPlayer::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() == QEvent::MouseMove) {
    auto [min_sec, max_sec] = StreamManager::stream()->timeRange().value_or(std::make_pair(StreamManager::stream()->minSeconds(), StreamManager::stream()->maxSeconds()));
    showThumbnail(min_sec + static_cast<QMouseEvent *>(event)->pos().x() * (max_sec - min_sec) / slider->width());
  } else if (event->type() == QEvent::Leave) {
    showThumbnail(-1);
  }
  return false;
}
