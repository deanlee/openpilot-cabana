#include "signal_tree.h"

#include "signal_editor.h"

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

  // Use a distinctive background for the whole row containing a QSpinBox or QLineEdit
  QString nodeBgColor = palette().color(QPalette::AlternateBase).name(QColor::HexArgb);
  setStyleSheet(QString("QSpinBox{background-color:%1;border:none;} QLineEdit{background-color:%1;}").arg(nodeBgColor));
}

void SignalTree::dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles) {
  // Bypass the slow call to QTreeView::dataChanged.
  QAbstractItemView::dataChanged(topLeft, bottomRight, roles);
}

void SignalTree::leaveEvent(QEvent* event) {
  QTreeView::leaveEvent(event);
  updateHighlight(nullptr);
}

void SignalTree::mouseMoveEvent(QMouseEvent* event) {
  QTreeView::mouseMoveEvent(event);

  const dbc::Signal* current_sig = nullptr;
  QModelIndex idx = indexAt(event->pos());

  if (idx.isValid()) {
    if (auto m = qobject_cast<SignalTreeModel*>(model())) {
      if (auto item = m->getItem(idx)) {
        current_sig = item->sig;
      }
    }
  }

  updateHighlight(current_sig);
}

void SignalTree::updateHighlight(const dbc::Signal* sig) {
  if (sig != last_sig) {
    last_sig = sig;
    emit highlightRequested(sig);
  }
}
