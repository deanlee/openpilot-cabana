#pragma once

#include <QLineEdit>
#include <QStringList>

#include "abstract.h"

class AscLogWidget : public AbstractStreamWidget {
  Q_OBJECT

 public:
  AscLogWidget(QWidget* parent = nullptr);
  AbstractStream* open() override;

 private:
  QLineEdit* file_edit_;
  QStringList file_paths_;
};
