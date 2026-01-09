#include "guide_overlay.h"

#include <QResizeEvent>
#include <QTextDocument>

GuideOverlay::GuideOverlay(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_NoSystemBackground, true);
  setAttribute(Qt::WA_TranslucentBackground, true);
  setAttribute(Qt::WA_DeleteOnClose);
  parent->installEventFilter(this);
}

void GuideOverlay::paintEvent(QPaintEvent* event) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor(0, 0, 0, 50));
  auto parent = parentWidget();
  drawHelpForWidget(painter, parent->findChild<MessageList*>());
  drawHelpForWidget(painter, parent->findChild<BinaryView*>());
  drawHelpForWidget(painter, parent->findChild<SignalEditor*>());
  drawHelpForWidget(painter, parent->findChild<ChartsWidget*>());
  drawHelpForWidget(painter, parent->findChild<VideoPlayer*>());
}

void GuideOverlay::drawHelpForWidget(QPainter& painter, QWidget* w) {
  if (w && w->isVisible() && !w->whatsThis().isEmpty()) {
    QPoint pt = mapFromGlobal(w->mapToGlobal(w->rect().center()));
    if (rect().contains(pt)) {
      QTextDocument document;
      document.setHtml(w->whatsThis());
      QSize doc_size = document.size().toSize();
      QPoint topleft = {pt.x() - doc_size.width() / 2, pt.y() - doc_size.height() / 2};
      painter.translate(topleft);
      painter.fillRect(QRect{{0, 0}, doc_size}, palette().toolTipBase());
      document.drawContents(&painter);
      painter.translate(-topleft);
    }
  }
}

bool GuideOverlay::eventFilter(QObject* obj, QEvent* event) {
  if (obj == parentWidget() && event->type() == QEvent::Resize) {
    QResizeEvent* resize_event = (QResizeEvent*)(event);
    setGeometry(QRect{QPoint(0, 0), resize_event->size()});
  }
  return false;
}

void GuideOverlay::mouseReleaseEvent(QMouseEvent* event) {
  close();
}
