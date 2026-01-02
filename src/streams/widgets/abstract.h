#pragma once
#include <QWidget>

#include "streams/abstractstream.h"

class AbstractStreamWidget : public QWidget {
  Q_OBJECT
public:
  AbstractStreamWidget(QWidget *parent = nullptr);
  virtual AbstractStream *open() = 0;

signals:
  void enableOpenButton(bool);
};
