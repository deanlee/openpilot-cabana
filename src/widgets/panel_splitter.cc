#include "panel_splitter.h"

#include <QEvent>

PanelSplitter::PanelSplitter(Qt::Orientation orientation, QWidget* parent) : QSplitter(orientation, parent) {
  setHandleWidth(3);

  // Recommended for smoother resizing
  setOpaqueResize(true);
  setChildrenCollapsible(false);
}

PanelSplitter::Handle::Handle(Qt::Orientation o, QSplitter* p) : QSplitterHandle(o, p) {
  setMouseTracking(true);
  setAttribute(Qt::WA_Hover);
}

void PanelSplitter::Handle::paintEvent(QPaintEvent* e) {
  QPainter p(this);
  if (underMouse() || is_dragging) {
    p.fillRect(rect(), palette().highlight());
    return;
  }

  p.setPen(QPen(palette().dark().color(), 1));
  const QRect r = rect();

  if (orientation() == Qt::Horizontal) {
    int x = r.center().x();
    p.drawLine(x, r.top(), x, r.bottom());
  } else {
    int y = r.center().y();
    p.drawLine(r.left(), y, r.right(), y);
  }
}

bool PanelSplitter::Handle::event(QEvent* e) {
  switch (e->type()) {
    case QEvent::HoverEnter:
    case QEvent::HoverLeave:
      update();
      break;
    default:
      break;
  }
  return QSplitterHandle::event(e);
}

void PanelSplitter::Handle::mousePressEvent(QMouseEvent* e) {
  is_dragging = true;
  update();
  QSplitterHandle::mousePressEvent(e);
}

void PanelSplitter::Handle::mouseReleaseEvent(QMouseEvent* e) {
  is_dragging = false;
  update();
  QSplitterHandle::mouseReleaseEvent(e);
}
