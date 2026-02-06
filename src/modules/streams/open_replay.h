#pragma once

#include <QCheckBox>
#include <QLineEdit>

#include "abstract.h"

class OpenReplayWidget : public AbstractStreamWidget {
  Q_OBJECT

 public:
  OpenReplayWidget(QWidget* parent = nullptr);
  AbstractStream* open() override;

 private:
  QLineEdit* route_edit;
  std::vector<QCheckBox*> cameras;
};
