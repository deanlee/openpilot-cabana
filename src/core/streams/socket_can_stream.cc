#include "socket_can_stream.h"

#include <QDebug>
#include <QThread>

SocketCanStream::SocketCanStream(QObject* parent, SocketCanStreamConfig config_) : LiveStream(parent), config(config_) {
  if (!available()) {
    throw std::runtime_error("SocketCAN plugin not available");
  }

  qDebug() << "Connecting to SocketCAN device" << config.device;
  if (!connect()) {
    throw std::runtime_error("Failed to connect to SocketCAN device");
  }
}

bool SocketCanStream::available() { return QCanBus::instance()->plugins().contains("socketcan"); }

bool SocketCanStream::connect() {
  // Connecting might generate some warnings about missing socketcan/libsocketcan libraries
  // These are expected and can be ignored, we don't need the advanced features of libsocketcan
  QString errorString;
  device.reset(QCanBus::instance()->createDevice("socketcan", config.device, &errorString));
  if (!device) {
    qDebug() << "Failed to create SocketCAN device" << errorString;
    return false;
  }

  device->setConfigurationParameter(QCanBusDevice::CanFdKey, true);

  if (!device->connectDevice()) {
    qDebug() << "Failed to connect to device";
    return false;
  }

  return true;
}

void SocketCanStream::streamThread() {
  while (!QThread::currentThread()->isInterruptionRequested()) {
    QThread::msleep(1);

    auto frames = device->readAllFrames();
    if (frames.size() == 0) continue;

    // Count valid frames first to avoid ghost entries in the capnp message
    size_t valid_count = 0;
    for (const auto& f : frames) {
      if (f.isValid()) ++valid_count;
    }
    if (valid_count == 0) continue;

    MessageBuilder msg;
    auto evt = msg.initEvent();
    auto canData = evt.initCan(valid_count);

    size_t j = 0;
    for (const auto& frame : frames) {
      if (!frame.isValid()) continue;

      canData[j].setAddress(frame.frameId());
      canData[j].setSrc(0);

      auto payload = frame.payload();
      canData[j].setDat(kj::arrayPtr((uint8_t*)payload.data(), payload.size()));
      ++j;
    }

    handleEvent(capnp::messageToFlatArray(msg));
  }
}
