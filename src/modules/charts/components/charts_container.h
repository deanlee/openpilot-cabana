#pragma once

#include <QDropEvent>
#include <QGridLayout>
#include <QWidget>

#include "modules/charts/chart_view.h"

enum class DropMode { None, Merge, InsertBefore, InsertAfter };

class ChartsContainer : public QWidget {
  Q_OBJECT
 public:
  ChartsContainer(QWidget* parent);
  void updateLayout(const QList<ChartView*>& current_charts, int column_count, bool force = false);
  void resetDragState();

 signals:
  void chartDropped(ChartView* chart, ChartView* after, DropMode mode);

 private:
  QSize minimumSizeHint() const override { return QSize(0, 0); }
  void resizeEvent(QResizeEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* e) override;
  void dropEvent(QDropEvent* event) override;
  void dragLeaveEvent(QDragLeaveEvent* event) override;

  void handleDragInteraction(const QPoint& pos);
  void handleDrop(ChartView* target, DropMode mode, QDropEvent* event);
  bool eventFilter(QObject* obj, QEvent* event) override;
  void paintEvent(QPaintEvent* ev) override;
  int calculateOptimalColumns() const;
  void reflowLayout();

  QGridLayout* grid_layout_;
  int current_column_count_ = -1;
  QList<ChartView*> active_charts_;
  ChartView* active_target = nullptr;
  DropMode drop_mode = DropMode::None;
};
