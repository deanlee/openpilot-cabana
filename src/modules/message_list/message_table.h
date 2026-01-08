#pragma once

#include <QTreeView>
#include <QWheelEvent>

class MessageTable : public QTreeView {
  Q_OBJECT
public:
  MessageTable(QWidget *parent);
  void updateLayout();

protected:
  void drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const override {}
  void dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles = QVector<int>()) override;
  void wheelEvent(QWheelEvent *event) override;
};
