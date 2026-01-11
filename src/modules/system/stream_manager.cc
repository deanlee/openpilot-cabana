#include "stream_manager.h"

#include <QDebug>
#include <QProgressDialog>

#include "system_relay.h"

StreamManager::StreamManager() : QObject(nullptr) {
  setStream(new DummyStream(this));
}

void StreamManager::setStream(AbstractStream* new_stream, const QString& dbc_file) {
  if (stream_) {
    // Stop delivery of signals to the manager during deletion
    stream_->disconnect(this);
    stream_->deleteLater();
    stream_ = nullptr;
  }
  stream_ = new_stream ? new_stream : new DummyStream(this);
  stream_->setParent(this);
  connect(stream_, &AbstractStream::eventsMerged, this, &StreamManager::eventsMerged);
  connect(stream_, &AbstractStream::paused, this, &StreamManager::paused);
  connect(stream_, &AbstractStream::resume, this, &StreamManager::resume);
  connect(stream_, &AbstractStream::seeking, this, &StreamManager::seeking);
  connect(stream_, &AbstractStream::seekedTo, this, &StreamManager::seekedTo);
  connect(stream_, &AbstractStream::timeRangeChanged, this, &StreamManager::timeRangeChanged);
  connect(stream_, &AbstractStream::snapshotsUpdated, this, &StreamManager::snapshotsUpdated);
  connect(stream_, &AbstractStream::sourcesUpdated, this, &StreamManager::sourcesUpdated);
  connect(stream_, &AbstractStream::qLogLoaded, this, &StreamManager::qLogLoaded);

  emit streamChanged();
  stream_->start();
  if (!isDummy()) {
    qInfo() << QString("Stream [%1] started").arg(stream_->routeName());
  }
}

void StreamManager::closeStream() {
  setStream(new DummyStream(this));
}

void StreamManager::shutdown() {
  if (stream_) {
    stream_->disconnect(this);
  }
}
