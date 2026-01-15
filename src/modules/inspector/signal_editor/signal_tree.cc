#include "signal_tree.h"
#include "signal_editor.h"

#include <QHeaderView>

SignalTree::SignalTree(QWidget* parent) : QTreeView(parent) {
  setFrameShape(QFrame::NoFrame);
  setHeaderHidden(true);
  setMouseTracking(true);
  setExpandsOnDoubleClick(true);
  setUniformRowHeights(true);
  setEditTriggers(QAbstractItemView::AllEditTriggers);
  viewport()->setMouseTracking(true);
  viewport()->setAttribute(Qt::WA_AlwaysShowToolTips, true);
  setToolTipDuration(1000);

  header()->setSectionResizeMode(0, QHeaderView::Fixed);
  header()->setSectionResizeMode(1, QHeaderView::Stretch);

  // Use a distinctive background for the whole row containing a QSpinBox or QLineEdit
  QString nodeBgColor = palette().color(QPalette::AlternateBase).name(QColor::HexArgb);
  setStyleSheet(QString("QSpinBox{background-color:%1;border:none;} QLineEdit{background-color:%1;}").arg(nodeBgColor));
}

void SignalTree::rowsInserted(const QModelIndex& parent, int start, int end) {
  ((SignalEditor*)parentWidget())->rowsChanged();
  // update widget geometries in QTreeView::rowsInserted
  QTreeView::rowsInserted(parent, start, end);
}

void SignalTree::dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles) {
  // Bypass the slow call to QTreeView::dataChanged.
  QAbstractItemView::dataChanged(topLeft, bottomRight, roles);
}

void SignalTree::leaveEvent(QEvent* event) {
  emit static_cast<SignalEditor*>(parentWidget())->highlight(nullptr);
  if (auto d = (SignalTreeDelegate*)(itemDelegate())) {
    d->clearHoverState();
    viewport()->update();
  }
  QTreeView::leaveEvent(event);
}

void SignalTree::mouseMoveEvent(QMouseEvent* event) {
  QTreeView::mouseMoveEvent(event);
  QModelIndex idx = indexAt(event->pos());
  if (!idx.isValid()) {
    if (auto d = (SignalTreeDelegate*)(itemDelegate())) {
      d->clearHoverState();
      viewport()->update();
    }
  }
}
