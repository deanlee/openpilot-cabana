#pragma once

#include <QTreeView>
#include <QWheelEvent>

class MessageTable : public QTreeView {
  Q_OBJECT
 public:
  explicit MessageTable(QWidget* parent);

 protected:
  void drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const override {}
  void dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight,
                   const QVector<int>& roles = {}) override;
  void wheelEvent(QWheelEvent* event) override;
};
