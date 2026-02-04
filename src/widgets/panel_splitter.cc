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
  if (underMouse() || is_dragging) {
    QPainter p(this);
    p.fillRect(rect(), palette().highlight());
    return;
  }

  QSplitterHandle::paintEvent(e);
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
