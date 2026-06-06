#include "message_table.h"

#include <QApplication>
#include <QScrollBar>

#include "message_model.h"

MessageTable::MessageTable(QWidget* parent) : QTreeView(parent) {
  setSortingEnabled(true);
  sortByColumn(MessageModel::Column::NAME, Qt::AscendingOrder);
  setAllColumnsShowFocus(true);
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setItemsExpandable(false);
  setIndentation(0);
  setRootIsDecorated(false);
  setAlternatingRowColors(true);
  setUniformRowHeights(true);
}

void MessageTable::dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles) {
  // Bypass the slow call to QTreeView::dataChanged.
  // QTreeView::dataChanged will invalidate the height cache and that's what we don't need in MessageTable.
  QAbstractItemView::dataChanged(topLeft, bottomRight, roles);
}

void MessageTable::wheelEvent(QWheelEvent* event) {
  if (event->modifiers().testFlag(Qt::ShiftModifier)) {
    QApplication::sendEvent(horizontalScrollBar(), event);
  } else {
    QTreeView::wheelEvent(event);
  }
}
