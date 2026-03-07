#pragma once

#include <QLineEdit>
#include <QStringList>

#include "abstract.h"

class CandumpLogWidget : public AbstractStreamWidget {
  Q_OBJECT

 public:
  CandumpLogWidget(QWidget* parent = nullptr);
  AbstractStream* open() override;

 private:
  QLineEdit* file_edit_;
  QStringList file_paths_;
};
