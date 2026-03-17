#include "live_stream.h"

#include <QThread>
#include <QTimerEvent>
#include <algorithm>
#include <fstream>
#include <memory>

#include "common/timing.h"
#include "common/util.h"
#include "modules/settings/settings.h"

struct LiveStream::Logger {
  Logger() : start_ts(seconds_since_epoch()), segment_num(-1) {}

  void write(kj::ArrayPtr<capnp::word> data) {
    int n = (seconds_since_epoch() - start_ts) / 60.0;
    if (std::exchange(segment_num, n) != segment_num) {
      QString dir = QString("%1/%2--%3")
                        .arg(settings.log_path)
                        .arg(QDateTime::fromSecsSinceEpoch(start_ts).toString("yyyy-MM-dd--hh-mm-ss"))
                        .arg(n);
      util::create_directories(dir.toStdString(), 0755);
      fs.reset(new std::ofstream((dir + "/rlog").toStdString(), std::ios::binary | std::ios::out));
    }

    auto bytes = data.asBytes();
    fs->write((const char*)bytes.begin(), bytes.size());
  }

  std::unique_ptr<std::ofstream> fs;
  int segment_num;
  uint64_t start_ts;
};

LiveStream::LiveStream(QObject* parent) : AbstractStream(parent) {
  if (settings.log_livestream) {
    logger_ = std::make_unique<Logger>();
  }
  stream_thread_ = new QThread(this);

  connect(&settings, &Settings::changed, this, &LiveStream::startFrameTimer);
  connect(stream_thread_, &QThread::started, [this]() { streamThread(); });
  connect(stream_thread_, &QThread::finished, stream_thread_, &QThread::deleteLater);
}

LiveStream::~LiveStream() { stop(); }

void LiveStream::start() {
  stream_thread_->start();
  startFrameTimer();
  begin_date_time_ = QDateTime::currentDateTime();
}

void LiveStream::stop() {
  if (!stream_thread_) return;

  frame_timer_.stop();
  stream_thread_->requestInterruption();
  stream_thread_->quit();
  stream_thread_->wait();
  stream_thread_ = nullptr;
}

void LiveStream::startFrameTimer() {
  frame_timer_.stop();
  frame_timer_.start(1000.0 / settings.fps, this);
}

// Called from the stream thread
void LiveStream::handleEvent(kj::ArrayPtr<capnp::word> data) {
  if (logger_) {
    logger_->write(data);
  }

  capnp::FlatArrayMessageReader reader(data);
  auto event = reader.getRoot<cereal::Event>();
  if (event.which() == cereal::Event::Which::CAN) {
    const uint64_t mono_ns = event.getLogMonoTime();
    std::lock_guard lk(recv_mutex_);
    for (const auto& c : event.getCan()) {
      recv_queue_.push_back(newEvent(mono_ns, c));
    }
  }
}

void LiveStream::timerEvent(QTimerEvent* event) {
  if (event->timerId() != frame_timer_.timerId()) {
    QObject::timerEvent(event);
    return;
  }

  drainQueue();
  if (!all_events_.empty()) {
    begin_ns_ = all_events_.front()->mono_ns;
    advancePlayback();
  }
}

void LiveStream::drainQueue() {
  std::vector<const CanEvent*> batch;
  {
    std::lock_guard lk(recv_mutex_);
    batch.swap(recv_queue_);
  }
  if (!batch.empty()) {
    mergeEvents(batch);
    latest_ns_ = std::max(latest_ns_, batch.back()->mono_ns);
  }
}

void LiveStream::advancePlayback() {
  // Initialize anchor on the first frame with data
  if (anchor_wall_ns_ == 0) {
    cursor_ns_ = all_events_.back()->mono_ns;
    resetAnchor();
  }

  if (paused_) return;

  const uint64_t target = playbackTarget();
  auto first = std::ranges::upper_bound(all_events_, cursor_ns_, {}, &CanEvent::mono_ns);
  auto last = std::ranges::upper_bound(first, all_events_.end(), target, {}, &CanEvent::mono_ns);

  for (auto it = first; it != last; ++it) {
    const CanEvent* e = *it;
    processNewMessage({e->src, e->address}, e->mono_ns, e->dat, e->size);
    cursor_ns_ = e->mono_ns;
  }

  at_live_edge_ = (cursor_ns_ >= latest_ns_);
  commitSnapshots();
}

void LiveStream::resetAnchor() {
  anchor_wall_ns_ = nanos_since_boot();
  anchor_can_ns_ = cursor_ns_;
}

uint64_t LiveStream::playbackTarget() const {
  // At normal speed on the live edge, skip clock math and process all available events
  if (at_live_edge_ && speed_ == 1.0) {
    return latest_ns_;
  }
  return anchor_can_ns_ + static_cast<uint64_t>((nanos_since_boot() - anchor_wall_ns_) * speed_);
}

void LiveStream::setSpeed(float speed) {
  if (speed_ != speed) {
    resetAnchor();
    speed_ = speed;
  }
}

void LiveStream::seekTo(double sec) {
  sec = std::max(0.0, sec);
  cursor_ns_ = std::min<uint64_t>(sec * 1e9 + begin_ns_, latest_ns_);
  at_live_edge_ = (cursor_ns_ >= latest_ns_);
  resetAnchor();
  emit seekedTo((cursor_ns_ - begin_ns_) / 1e9);
}

void LiveStream::pause(bool pause) {
  if (paused_ != pause) {
    paused_ = pause;
    resetAnchor();
    emit(pause ? paused() : resume());
  }
}
