#pragma once

#include <QWidget>
#include <QGridLayout>
#include <QDragEnterEvent>
#include <QDropEvent>

class ChartsPanel;
class ChartView;

class ChartsContainer : public QWidget {
public:
  ChartsContainer(ChartsPanel *parent);
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  void dragLeaveEvent(QDragLeaveEvent *event) override { drawDropIndicator({}); }
  void drawDropIndicator(const QPoint &pt) { drop_indictor_pos = pt; update(); }
  void paintEvent(QPaintEvent *ev) override;
  ChartView *getDropAfter(const QPoint &pos) const;

  QGridLayout *charts_layout;
  ChartsPanel *charts_widget;
  QPoint drop_indictor_pos;
};
