#pragma once

#include <QLineEdit>
#include <QStringList>

#include "abstract.h"

class OpenTrcLogWidget : public AbstractStreamWidget {
  Q_OBJECT

 public:
  OpenTrcLogWidget(QWidget* parent = nullptr);
  AbstractStream* open() override;

 private:
  QLineEdit* file_edit_;
  QStringList file_paths_;
};
