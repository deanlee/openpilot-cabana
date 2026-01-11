#pragma once

#include <memory>

#include <QtSerialBus/QCanBus>
#include <QtSerialBus/QCanBusDevice>
#include <QtSerialBus/QCanBusDeviceInfo>

#include "live_stream.h"

struct SocketCanStreamConfig {
  QString device = ""; // TODO: support multiple devices/buses at once
};

class SocketCanStream : public LiveStream {
  Q_OBJECT
public:
  SocketCanStream(QObject *parent, SocketCanStreamConfig config_ = {});
  ~SocketCanStream() { stop(); }
  static bool available();

  inline QString routeName() const override {
    return QString("Live Streaming From Socket CAN %1").arg(config.device);
  }

protected:
  void streamThread() override;
  bool connect();

  SocketCanStreamConfig config = {};
  std::unique_ptr<QCanBusDevice> device;
};
