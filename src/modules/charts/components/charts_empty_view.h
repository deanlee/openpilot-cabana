#pragma once

#include <QPaintEvent>
#include <QWidget>

class ChartsEmptyView : public QWidget {
  Q_OBJECT
 public:
  explicit ChartsEmptyView(QWidget* parent = nullptr);
  void paintEvent(QPaintEvent* ev) override;
};
