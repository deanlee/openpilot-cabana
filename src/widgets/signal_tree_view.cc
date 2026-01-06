#include "signal_tree_view.h"
#include "signalview.h"

SignalTreeView::SignalTreeView(QWidget* parent) : QTreeView(parent) {
  setFrameShape(QFrame::NoFrame);
  setHeaderHidden(true);
  setMouseTracking(true);
  setExpandsOnDoubleClick(true);
  setEditTriggers(QAbstractItemView::AllEditTriggers);
  viewport()->setMouseTracking(true);
  viewport()->setAttribute(Qt::WA_AlwaysShowToolTips, true);
  setToolTipDuration(1000);
}

void SignalTreeView::rowsInserted(const QModelIndex& parent, int start, int end) {
  ((SignalView*)parentWidget())->rowsChanged();
  // update widget geometries in QTreeView::rowsInserted
  QTreeView::rowsInserted(parent, start, end);
}

void SignalTreeView::dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles) {
  // Bypass the slow call to QTreeView::dataChanged.
  QAbstractItemView::dataChanged(topLeft, bottomRight, roles);
}

void SignalTreeView::leaveEvent(QEvent* event) {
  emit static_cast<SignalView*>(parentWidget())->highlight(nullptr);
  if (auto d = (SignalTreeDelegate*)(itemDelegate())) {
    d->clearHoverState();
    viewport()->update();
  }
  QTreeView::leaveEvent(event);
}

void SignalTreeView::mouseMoveEvent(QMouseEvent* event) {
  QTreeView::mouseMoveEvent(event);
  QModelIndex idx = indexAt(event->pos());
  if (!idx.isValid()) {
    if (auto d = (SignalTreeDelegate*)(itemDelegate())) {
      d->clearHoverState();
      viewport()->update();
    }
  }
}
