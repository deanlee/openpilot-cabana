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
  setUniformRowHeights(settings.multiple_lines_hex == false);
}

void MessageTable::dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles) {
  // Bypass the slow call to QTreeView::dataChanged.
  // QTreeView::dataChanged will invalidate the height cache and that's what we don't need in MessageTable.
  QAbstractItemView::dataChanged(topLeft, bottomRight, roles);
}

void MessageTable::updateLayout() {
  auto delegate = qobject_cast<MessageDelegate*>(itemDelegateForColumn(MessageModel::Column::DATA));
  if (!delegate) return;

  bool multi_line = settings.multiple_lines_hex;
  delegate->setMultipleLines(multi_line);
  setUniformRowHeights(!multi_line);

  int max_bytes = 8;
  if (!multi_line) {
    for (const auto& [_, m] : StreamManager::stream()->snapshots()) {
      max_bytes = std::max<int>(max_bytes, m->dat.size());
    }
  }

  int target_width = delegate->sizeForBytes(max_bytes).width();
  if (header()->sectionSize(MessageModel::Column::DATA) != target_width) {
    header()->resizeSection(MessageModel::Column::DATA, target_width);
  }
  doItemsLayout();
}

void MessageTable::wheelEvent(QWheelEvent* event) {
  if (event->modifiers() == Qt::ShiftModifier) {
    QApplication::sendEvent(horizontalScrollBar(), event);
  } else {
    QTreeView::wheelEvent(event);
  }
}
