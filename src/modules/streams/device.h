#pragma once
#include <QButtonGroup>
#include <QLineEdit>

#include "abstract.h"
#include "core/streams/abstract_stream.h"

class DeviceWidget : public AbstractStreamWidget {
  Q_OBJECT

 public:
  DeviceWidget(QWidget* parent = nullptr);
  AbstractStream* open() override;

 private:
  QLineEdit* ip_address;
  QButtonGroup* group;
};
