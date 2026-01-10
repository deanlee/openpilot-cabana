#include "message_table.h"

#include <QApplication>
#include <QHeaderView>
#include <QScrollBar>

#include "message_delegate.h"
#include "message_model.h"
#include "modules/settings/settings.h"
#include "modules/system/stream_manager.h"

MessageTable::MessageTable(QWidget* parent) : QTreeView(parent) {
  setSortingEnabled(true);
  sortByColumn(MessageModel::Column::NAME, Qt::AscendingOrder);
  setAllColumnsShowFocus(true);
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setItemsExpandable(false);
  setIndentation(0);
  setRootIsDecorated(false);
  setAlternatingRowColors(true);
}

void MessageTable::dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles) {
  // Bypass the slow call to QTreeView::dataChanged.
  // QTreeView::dataChanged will invalidate the height cache and that's what we don't need in MessageTable.
  QAbstractItemView::dataChanged(topLeft, bottomRight, roles);
}

void MessageTable::updateLayout() {
  bool multiLine = settings.multiple_lines_hex;
  auto delegate = static_cast<MessageDelegate*>(itemDelegate());
  delegate->setMultipleLines(multiLine);

  setUniformRowHeights(!multiLine);

  int max_bytes = 8;
  if (!multiLine) {
    for (const auto& [_, m] : StreamManager::stream()->snapshots()) {
      max_bytes = std::max<int>(max_bytes, m->dat.size());
    }
  }
  header()->resizeSection(MessageModel::Column::DATA, delegate->sizeForBytes(max_bytes).width());
  doItemsLayout();
}

void MessageTable::wheelEvent(QWheelEvent* event) {
  if (event->modifiers() == Qt::ShiftModifier) {
    QApplication::sendEvent(horizontalScrollBar(), event);
  } else {
    QTreeView::wheelEvent(event);
  }
}
