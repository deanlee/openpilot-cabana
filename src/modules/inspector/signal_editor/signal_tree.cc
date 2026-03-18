#include "signal_tree.h"

#include "signal_editor.h"
#include "signal_tree_delegate.h"

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

  QString nodeBgColor = palette().color(QPalette::AlternateBase).name(QColor::HexArgb);
  setStyleSheet(QString("QSpinBox{background-color:%1;border:none;} QLineEdit{background-color:%1;}").arg(nodeBgColor));
}

void SignalTree::dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles) {
  // Bypass the slow call to QTreeView::dataChanged
  QAbstractItemView::dataChanged(topLeft, bottomRight, roles);
}

void SignalTree::leaveEvent(QEvent* event) {
  QTreeView::leaveEvent(event);
  updateHighlight(nullptr);
  static_cast<SignalTreeDelegate*>(itemDelegate())->clearHoverState();
}

void SignalTree::mouseMoveEvent(QMouseEvent* event) {
  QTreeView::mouseMoveEvent(event);

  const dbc::Signal* currentSig = nullptr;
  QModelIndex idx = indexAt(event->pos());

  if (idx.isValid()) {
    if (auto* item = static_cast<TreeItem*>(idx.internalPointer())) {
      if (auto* sigItem = dynamic_cast<SignalItem*>(item)) {
        currentSig = sigItem->sig;
      } else if (auto* propItem = dynamic_cast<PropertyItem*>(item)) {
        currentSig = propItem->sig;
      }
    }
  } else {
    static_cast<SignalTreeDelegate*>(itemDelegate())->clearHoverState();
  }

  updateHighlight(currentSig);
}

void SignalTree::updateHighlight(const dbc::Signal* sig) {
  if (sig != lastSig_) {
    lastSig_ = sig;
    emit highlightRequested(sig);
  }
}

void SignalTree::paintEvent(QPaintEvent* event) {
  QTreeView::paintEvent(event);

  if (!model() || model()->rowCount(rootIndex()) > 0) return;

  QPainter p(viewport());
  p.setPen(palette().color(QPalette::PlaceholderText));

  QFont font = p.font();
  font.setBold(true);
  p.setFont(font);
  p.drawText(viewport()->rect().adjusted(0, -15, 0, -15), Qt::AlignCenter, tr("No Signals Defined"));

  font.setBold(false);
  p.setFont(font);
  p.drawText(viewport()->rect().adjusted(20, 15, -20, 15), Qt::AlignCenter | Qt::TextWordWrap,
             tr("Drag bits in the binary view above to create a signal."));
}
