#pragma once
#include <QWidget>

class TimeLabel : public QWidget {
  Q_OBJECT
 public:
  TimeLabel(QWidget* parent = nullptr);

  void setText(const QString& new_text);

  QString text;
 signals:
  void clicked();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override {
    emit clicked();
  }
};
