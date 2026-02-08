#include "charts_scroll_area.h"

#include <QApplication>
#include <QMouseEvent>
#include <QScrollBar>

#include "modules/settings/settings.h"

ChartsScrollArea::ChartsScrollArea(QWidget* parent) : QScrollArea(parent) {
  setFrameStyle(QFrame::NoFrame);
  setWidgetResizable(true);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  viewport()->setBackgroundRole(QPalette::Base);

  auto_scroll_timer_ = new QTimer(this);
  connect(auto_scroll_timer_, &QTimer::timeout, this, &ChartsScrollArea::doAutoScroll);
}

void ChartsScrollArea::startAutoScroll() {
  if (!auto_scroll_timer_->isActive()) {
    auto_scroll_timer_->start(kAutoScrollIntervalMs);
  }
}

void ChartsScrollArea::stopAutoScroll() { auto_scroll_timer_->stop(); }

int ChartsScrollArea::autoScrollDelta(int pos, int viewportHeight) const {
  if (pos < kAutoScrollMargin) {
    // Scroll up — accelerate quadratically near the edge
    double ratio = double(kAutoScrollMargin - pos) / kAutoScrollMargin;
    return -qBound(kMinScrollSpeed, int(ratio * ratio * kMaxScrollSpeed), kMaxScrollSpeed);
  }
  if (pos > viewportHeight - kAutoScrollMargin) {
    // Scroll down
    double ratio = double(pos - (viewportHeight - kAutoScrollMargin)) / kAutoScrollMargin;
    return qBound(kMinScrollSpeed, int(ratio * ratio * kMaxScrollSpeed), kMaxScrollSpeed);
  }
  return 0;
}

void ChartsScrollArea::doAutoScroll() {
  const QPoint global_pos = QCursor::pos();
  const QPoint local_pos = viewport()->mapFromGlobal(global_pos);
  const int delta = autoScrollDelta(local_pos.y(), viewport()->height());

  if (delta == 0) {
    stopAutoScroll();
    return;
  }

  QScrollBar* scroll = verticalScrollBar();
  const int old_val = scroll->value();
  scroll->setValue(old_val + delta);

  if (scroll->value() != old_val) {
    // Synthesize a move event so the container updates drop indicators
    QMouseEvent move(QEvent::MouseMove, widget()->mapFromGlobal(global_pos),
                     global_pos, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(widget(), &move);
  }
}
