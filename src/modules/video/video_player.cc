#include "video_player.h"

#include <QAction>
#include <QActionGroup>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QStyle>
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

  setupConnections();

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

void VideoPlayer::setupConnections() {
  auto &sm = StreamManager::instance();
  connect(&sm, &StreamManager::streamChanged, this, &VideoPlayer::onStreamChanged);
  connect(&sm, &StreamManager::paused, this, &VideoPlayer::updatePlayBtnState);
  connect(&sm, &StreamManager::resume, this, &VideoPlayer::updatePlayBtnState);
  connect(&sm, &StreamManager::snapshotsUpdated, this, &VideoPlayer::updateState);
  connect(&sm, &StreamManager::seeking, this, &VideoPlayer::updateState);
  connect(&sm, &StreamManager::timeRangeChanged, this, &VideoPlayer::timeRangeChanged);
}

void VideoPlayer::createPlaybackController() {
  auto *bar = new QWidget(this);

  int margin = style()->pixelMetric(QStyle::PM_ToolBarItemMargin);
  int spacing = style()->pixelMetric(QStyle::PM_ToolBarItemSpacing);

  auto* h_layout = new QHBoxLayout(bar);
  h_layout->setContentsMargins(margin, margin, margin, margin);
  h_layout->setSpacing(spacing);

  // Left: Navigation
  h_layout->addWidget(createToolButton("step-back", tr("Seek back"), []() {
    StreamManager::stream()->seekTo(StreamManager::stream()->currentSec() - 1);
  }));

  play_toggle_btn = createToolButton("play", tr("Play"), []() {
    StreamManager::stream()->pause(!StreamManager::stream()->isPaused());
  });
  h_layout->addWidget(play_toggle_btn);

  h_layout->addWidget(createToolButton("step-forward", tr("Seek forward"), []() {
    StreamManager::stream()->seekTo(StreamManager::stream()->currentSec() + 1);
  }));

  // Center: Time Label
  time_label = new TimeLabel();
  time_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  h_layout->addWidget(time_label);
  connect(time_label, &TimeLabel::clicked, this, [this]() {
    settings.absolute_time = !settings.absolute_time;
    time_label->setToolTip(settings.absolute_time ? tr("Elapsed time") : tr("Absolute time"));
    updateState();
  });

  // Right: Settings & Info
  loop_btn = createToolButton("repeat", tr("Loop playback"), [this]() { loopPlaybackClicked(); });
  h_layout->addWidget(loop_btn);

  createSpeedDropdown();
  h_layout->addWidget(speed_btn);

  skip_to_end_btn = createToolButton("skip-forward", tr("Skip to end"), []() {
    auto s = StreamManager::stream();
    s->setSpeed(1.0);
    s->pause(false);
    s->seekTo(s->maxSeconds() + 1);
  });
  h_layout->addWidget(skip_to_end_btn);

  route_info_btn = createToolButton("info", tr("View route details"), [this]() { showRouteInfo(); });
  h_layout->addWidget(route_info_btn);

  layout()->addWidget(bar);
}

QToolButton* VideoPlayer::createToolButton(const QString &icon, const QString &tip, std::function<void()> cb) {
  int icon_size = style()->pixelMetric(QStyle::PM_SmallIconSize);
  auto *btn = new QToolButton(this);
  btn->setIcon(utils::icon(icon, QSize(icon_size, icon_size)));
  btn->setToolTip(tip);
  btn->setAutoRaise(true);
  btn->setIconSize(QSize(icon_size, icon_size));
  if (cb) connect(btn, &QToolButton::clicked, this, cb);
  return btn;
}

void VideoPlayer::createSpeedDropdown() {
  speed_btn = new QToolButton(this);
  speed_btn->setMenu(new QMenu(speed_btn));
  speed_btn->setPopupMode(QToolButton::InstantPopup);
  auto font = speed_btn->font();
  font.setBold(true);
  speed_btn->setFont(font);
  speed_btn->setAutoRaise(true);

  auto *speed_group = new QActionGroup(this);
  for (float speed : {0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 0.8, 1.0, 2.0, 3.0, 5.0}) {
    auto *act = speed_btn->menu()->addAction(QString("%1x").arg(speed), this, [this, speed]() {
      StreamManager::stream()->setSpeed(speed);
      speed_btn->setText(QString("%1x ").arg(speed));
      speed_btn->setToolTip(tr("Playback Speed: %1x").arg(speed));
    });
    act->setCheckable(true);
    speed_group->addAction(act);
    if (speed == 1.0) act->setChecked(true);
  }
  speed_btn->setText("1.0x ");
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

  l->addWidget(slider = new TimelineSlider(w));
  slider->setRange(StreamManager::stream()->minSeconds(), StreamManager::stream()->maxSeconds());

  connect(slider, &TimelineSlider::timeHovered, this, &VideoPlayer::showThumbnail);
  connect(&StreamManager::instance(), &StreamManager::paused, cam_widget, [c = cam_widget]() { c->update(); });
  connect(&StreamManager::instance(), &StreamManager::eventsMerged, this, [this]() { slider->update(); });
  connect(&StreamManager::instance(), &StreamManager::qLogLoaded, cam_widget, &PlaybackCameraView::parseQLog, Qt::QueuedConnection);
  connect(cam_widget, &PlaybackCameraView::clicked, []() { StreamManager::stream()->pause(!StreamManager::stream()->isPaused()); });
  connect(cam_widget, &PlaybackCameraView::vipcAvailableStreamsUpdated, this, &VideoPlayer::vipcAvailableStreamsUpdated);
  connect(camera_tab, &QTabBar::currentChanged, [this](int index) {
    if (index != -1) cam_widget->setStreamType((VisionStreamType)camera_tab->tabData(index).toInt());
  });
  return w;
}

void VideoPlayer::onStreamChanged() {
  timeRangeChanged();
  updateState();
  updatePlayBtnState();
  bool is_live = StreamManager::stream()->liveStreaming();

  camera_widget->setVisible(!is_live);
  loop_btn->setVisible(!is_live);
  route_info_btn->setVisible(!is_live);
  skip_to_end_btn->setVisible(is_live);
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
  loop_btn->setIcon(utils::icon(!is_looping ? "repeat" : "repeat-1"));
}

void VideoPlayer::timeRangeChanged() {
  const auto time_range = StreamManager::stream()->timeRange();
  if (StreamManager::stream()->liveStreaming()) {
    skip_to_end_btn->setEnabled(!time_range.has_value());
    return;
  }
  time_range ? slider->setRange(time_range->first, time_range->second)
             : slider->setRange(StreamManager::stream()->minSeconds(), StreamManager::stream()->maxSeconds());
  updateState();
}

QString VideoPlayer::formatTime(double sec, bool include_milliseconds) {
  if (settings.absolute_time)
    sec = StreamManager::stream()->beginDateTime().addMSecs(sec * 1000).toMSecsSinceEpoch() / 1000.0;
  return utils::formatSeconds(sec, include_milliseconds, settings.absolute_time);
}

void VideoPlayer::updateState() {
  if (slider) {
    slider->setTime(StreamManager::stream()->currentSec());
    if (camera_tab->count() == 0) {  //  No streams available
      cam_widget->update();          // Manually refresh to show alert events
    }
    time_label->setText(QString("%1 / %2").arg(formatTime(StreamManager::stream()->currentSec(), true),
                                             formatTime(slider->maximum())));
  } else {
    time_label->setText(formatTime(StreamManager::stream()->currentSec(), true));
  }
}

void VideoPlayer::updatePlayBtnState() {
  play_toggle_btn->setIcon(utils::icon(StreamManager::stream()->isPaused() ? "play" : "pause"));
  play_toggle_btn->setToolTip(StreamManager::stream()->isPaused() ? tr("Play") : tr("Pause"));
}

void VideoPlayer::showThumbnail(double seconds) {
  if (StreamManager::stream()->liveStreaming()) return;

  cam_widget->thumbnail_dispaly_time = seconds;
  cam_widget->update();
}

void VideoPlayer::showRouteInfo() {
  RouteInfoDlg *route_info = new RouteInfoDlg(this);
  route_info->setAttribute(Qt::WA_DeleteOnClose);
  route_info->show();
}
