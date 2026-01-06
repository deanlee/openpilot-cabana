#pragma once

#include <QTreeView>


struct SignalTreeView : public QTreeView {
 public:
  SignalTreeView(QWidget* parent);
  void rowsInserted(const QModelIndex& parent, int start, int end) override;
  void dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles = QVector<int>()) override;
  void leaveEvent(QEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
};
