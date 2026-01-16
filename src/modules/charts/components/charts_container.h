#pragma once

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGridLayout>
#include <QWidget>

class ChartView;

class ChartsContainer : public QWidget {
  Q_OBJECT
 public:
  ChartsContainer(QWidget* parent);
  ChartView* getDropAfter(const QPoint& pos) const;
  void updateLayout(const QList<ChartView*>& current_charts, int column_count, bool force = false);

 signals:
  void chartDropped(ChartView* chart, ChartView* after);

 private:
  void resizeEvent(QResizeEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragLeaveEvent(QDragLeaveEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  void updateDropIndicator(const QPoint& pt);
  void paintEvent(QPaintEvent* ev) override;
  int calculateOptimalColumns() const;
  void reflowLayout();

  QGridLayout* grid_layout_;
  QPoint drop_indictor_pos;
  int current_column_count_ = -1;
  QList<ChartView*> active_charts_;
};
