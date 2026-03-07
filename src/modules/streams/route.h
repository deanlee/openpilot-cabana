#pragma once

#include <QCheckBox>
#include <QLineEdit>

#include "abstract.h"

class RouteWidget : public AbstractStreamWidget {
  Q_OBJECT

 public:
  RouteWidget(QWidget* parent = nullptr);
  AbstractStream* open() override;

 private:
  QLineEdit* route_edit;
  std::vector<QCheckBox*> cameras;
};
