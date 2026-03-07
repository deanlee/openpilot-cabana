#pragma once

#include <QComboBox>

#include "abstract.h"
#include "core/streams/socket_can_stream.h"

class SocketCanWidget : public AbstractStreamWidget {
  Q_OBJECT

 public:
  SocketCanWidget(QWidget* parent = nullptr);
  AbstractStream* open() override;

 private:
  void refreshDevices();

  QComboBox* device_edit;
  SocketCanStreamConfig config = {};
};
