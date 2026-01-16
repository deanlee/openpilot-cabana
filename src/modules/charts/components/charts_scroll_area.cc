#include "charts_scroll_area.h"

#include <QApplication>
#include <QScrollBar>

#include "charts_container.h"
#include "modules/charts/chart_view.h"
#include "modules/settings/settings.h"

ChartsScrollArea::ChartsScrollArea(QWidget* parent) : QScrollArea(parent) {
  setFrameStyle(QFrame::NoFrame);
  setWidgetResizable(true);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  viewport()->setBackgroundRole(QPalette::Base);

  // We own the container widget that actually holds the grid
  container_ = new ChartsContainer(this);
  setWidget(container_);

  auto_scroll_timer_ = new QTimer(this);
  connect(auto_scroll_timer_, &QTimer::timeout, this, &ChartsScrollArea::doAutoScroll);
}

void ChartsScrollArea::startAutoScroll() {
  auto_scroll_timer_->start(50);
}

void ChartsScrollArea::stopAutoScroll() {
  auto_scroll_timer_->stop();
  auto_scroll_count_ = 0;
}

void ChartsScrollArea::doAutoScroll() {
  QScrollBar* scroll = verticalScrollBar();
  if (auto_scroll_count_ < scroll->pageStep()) {
    ++auto_scroll_count_;
  }

  int value = scroll->value();
  QPoint pos = viewport()->mapFromGlobal(QCursor::pos());
  QRect area = viewport()->rect();

  if (pos.y() - area.top() < settings.chart_height / 2) {
    scroll->setValue(value - auto_scroll_count_);
  } else if (area.bottom() - pos.y() < settings.chart_height / 2) {
    scroll->setValue(value + auto_scroll_count_);
  }
  bool vertical_unchanged = value == scroll->value();
  if (vertical_unchanged) {
    stopAutoScroll();
  } else {
    // mouseMoveEvent to updates the drag-selection rectangle
    const QPoint globalPos = viewport()->mapToGlobal(pos);
    const QPoint windowPos = window()->mapFromGlobal(globalPos);
    QMouseEvent mm(QEvent::MouseMove, pos, windowPos, globalPos,
                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier, Qt::MouseEventSynthesizedByQt);
    QApplication::sendEvent(viewport(), &mm);
  }
}
