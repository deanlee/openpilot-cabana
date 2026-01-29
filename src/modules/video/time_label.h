#pragma once

#include <QFont>
#include <QWidget>

class TimeLabel : public QWidget {
  Q_OBJECT
 public:
  TimeLabel(QWidget* parent = nullptr);
  void setTime(double cur, double total = -1);

 protected:
  QString formatTime(double sec, bool include_milliseconds = false);
  void updateTime();
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;

  double current_sec = 0;
  double total_sec = -1;
  QString current_sec_text;
  QString total_sec_text;
  QFont fixed_font;
  QFont bold_font;
  int cur_time_width = 0;
};
