#pragma once

#include <QLineEdit>
#include <QStringList>

#include "abstract.h"

class OpenCandumpLogWidget : public AbstractStreamWidget {
  Q_OBJECT

 public:
  OpenCandumpLogWidget(QWidget* parent = nullptr);
  AbstractStream* open() override;

 private:
  QLineEdit* file_edit_;
  QStringList file_paths_;
};
