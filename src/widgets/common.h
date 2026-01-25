#pragma once

#include <QLabel>
#include <QSlider>
#include <QTabBar>

class TabBar : public QTabBar {
  Q_OBJECT

 public:
  TabBar(QWidget* parent) : QTabBar(parent) {}
  int addTab(const QString& text);

 private:
  void closeTabClicked();
};

class LogSlider : public QSlider {
  Q_OBJECT

public:
  LogSlider(double factor, Qt::Orientation orientation, QWidget *parent = nullptr) : factor(factor), QSlider(orientation, parent) {}
  void setRange(double min, double max);
  int value() const;
  void setValue(int v);

private:
  double factor, log_min = 0, log_max = 1;
};

class ElidedLabel : public QLabel {
public:
  ElidedLabel(QWidget *parent);
  void paintEvent(QPaintEvent *event) override;
};

QFrame *createVLine(QWidget *parent);
