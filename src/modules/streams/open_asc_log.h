#pragma once

#include <QLineEdit>
#include <QStringList>

#include "abstract.h"

class OpenAscLogWidget : public AbstractStreamWidget {
  Q_OBJECT

 public:
  OpenAscLogWidget(QWidget* parent = nullptr);
  AbstractStream* open() override;

 private:
  QLineEdit* file_edit_;
  QStringList file_paths_;
};
