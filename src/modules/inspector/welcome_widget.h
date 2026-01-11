#pragma once
#include <QWidget>

class WelcomeWidget : public QWidget {
  Q_OBJECT
 public:
  WelcomeWidget(QWidget* parent = nullptr) : QWidget(parent) {}
  void paintEvent(QPaintEvent* event) override;
};
