#include "replay_stream.h"

#include <QMessageBox>
#include <QTimer>

#include "common/timing.h"
#include "common/util.h"
#include "modules/settings/settings.h"

ReplayStream::ReplayStream(QObject *parent) : AbstractStream(parent) {
  unsetenv("ZMQ");
  setenv("COMMA_CACHE", "/tmp/comma_download_cache", 1);

  // TODO: Remove when OpenpilotPrefix supports ZMQ
#ifndef __APPLE__
  op_prefix = std::make_unique<OpenpilotPrefix>();
#endif

  ui_update_timer = new QTimer(this);
  ui_update_timer->setInterval(1000 / settings.fps);

  connect(&settings, &Settings::changed, this, [this]() {
    if (replay) replay->setSegmentCacheLimit(settings.max_cached_minutes);
    if (ui_update_timer) {
      ui_update_timer->setInterval(1000 / settings.fps);
    }
  });
  connect(ui_update_timer, &QTimer::timeout, this, &ReplayStream::commitSnapshots);

  ui_update_timer->start();
}

void ReplayStream::mergeSegments() {
  auto event_data = replay->getEventData();
  for (const auto &[n, seg] : event_data->segments) {
    if (!processed_segments.count(n)) {
      processed_segments.insert(n);

      std::vector<const CanEvent *> new_events;
      new_events.reserve(seg->log->events.size());
      for (const Event &e : seg->log->events) {
        if (e.which == cereal::Event::Which::CAN) {
          capnp::FlatArrayMessageReader reader(e.data);
          auto event = reader.getRoot<cereal::Event>();
          for (const auto &c : event.getCan()) {
            new_events.push_back(newEvent(e.mono_time, c));
          }
        }
      }
      mergeEvents(new_events);
    }
  }
}

bool ReplayStream::loadRoute(const QString &route, const QString &data_dir, uint32_t replay_flags, bool auto_source) {
  ReplayConfig cfg = {
    .data_dir = data_dir.toStdString(),
    .route = route.toStdString(),
    .flags = replay_flags | REPLAY_FLAG_LOW_MEMORY,
    .cache_segments = settings.max_cached_minutes,
    .auto_source = auto_source,
    .allow = {"can", "roadEncodeIdx", "driverEncodeIdx", "wideRoadEncodeIdx", "carParams"},
  };
  replay.reset(new Replay(cfg));
  replay->installEventFilter([this](const Event *event) { return eventFilter(event); });

  // Forward replay callbacks to corresponding Qt signals.
  replay->onSeeking = [this](double sec) { emit seeking(sec); };
  replay->onSeekedTo = [this](double sec) {
    emit seekedTo(sec);
    waitForSeekFinshed();
  };
  replay->onQLogLoaded = [this](std::shared_ptr<LogReader> qlog) { emit qLogLoaded(qlog); };
  replay->onSegmentsMerged = [this]() { QMetaObject::invokeMethod(this, &ReplayStream::mergeSegments, Qt::BlockingQueuedConnection); };

  bool success = replay->load();
  if (!success) {
    if (replay->lastRouteError() == RouteLoadError::Unauthorized) {
      auto auth_content = util::read_file(util::getenv("HOME") + "/.comma/auth.json");
      QString message;
      if (auth_content.empty()) {
        message = "Authentication Required. Please run the following command to authenticate:\n\n"
                  "python3 tools/lib/auth.py\n\n"
                  "This will grant access to routes from your comma account.";
      } else {
        message = tr("Access Denied. You do not have permission to access route:\n\n%1\n\n"
                     "This is likely a private route.").arg(route);
      }
      QMessageBox::warning(nullptr, tr("Access Denied"), message);
    } else if (replay->lastRouteError() == RouteLoadError::NetworkError) {
      QMessageBox::warning(nullptr, tr("Network Error"),
                          tr("Unable to load the route:\n\n %1.\n\nPlease check your network connection and try again.").arg(route));
    } else if (replay->lastRouteError() == RouteLoadError::FileNotFound) {
      QMessageBox::warning(nullptr, tr("Route Not Found"),
                           tr("The specified route could not be found:\n\n %1.\n\nPlease check the route name and try again.").arg(route));
    } else {
      QMessageBox::warning(nullptr, tr("Route Load Failed"), tr("Failed to load route: '%1'").arg(route));
    }
  }
  return success;
}

bool ReplayStream::eventFilter(const Event *event) {
  if (event->which == cereal::Event::Which::CAN) {
    double current_sec = toSeconds(event->mono_time);
    capnp::FlatArrayMessageReader reader(event->data);
    auto e = reader.getRoot<cereal::Event>();
    for (const auto &c : e.getCan()) {
      MessageId id(c.getSrc(), c.getAddress());
      const auto dat = c.getDat();
      processNewMessage(id, current_sec, (const uint8_t*)dat.begin(), dat.size());
    }
  }
  return true;
}

void ReplayStream::pause(bool pause) {
  replay->pause(pause);
  emit(pause ? paused() : resume());
}
