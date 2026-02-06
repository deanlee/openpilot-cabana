#pragma once
#include <QButtonGroup>
#include <QLineEdit>

#include "abstract.h"
#include "core/streams/abstract_stream.h"

class OpenDeviceWidget : public AbstractStreamWidget {
  Q_OBJECT

 public:
  OpenDeviceWidget(QWidget* parent = nullptr);
  AbstractStream* open() override;

 private:
  QLineEdit* ip_address;
  QButtonGroup* group;
};
