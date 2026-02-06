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
  if (!auto_scroll_timer_->isActive()) {
    auto_scroll_timer_->start(20);  // Smoother 50Hz update
  }
}

void ChartsScrollArea::stopAutoScroll() { auto_scroll_timer_->stop(); }

void ChartsScrollArea::doAutoScroll() {
  QScrollBar* scroll = verticalScrollBar();
  QPoint global_pos = QCursor::pos();
  QPoint local_pos = viewport()->mapFromGlobal(global_pos);
  QRect area = viewport()->rect();

  int margin = 60;  // Sensitivity zone
  int delta = 0;

  if (local_pos.y() < margin) {
    delta = -qBound(2, (margin - local_pos.y()) / 2, 30);
  } else if (local_pos.y() > area.height() - margin) {
    delta = qBound(2, (margin - (area.height() - local_pos.y())) / 2, 30);
  }

  if (delta == 0) {
    stopAutoScroll();
    return;
  }

  int old_val = scroll->value();
  scroll->setValue(old_val + delta);

  if (scroll->value() != old_val) {
    // Synthesize a move event to the container to update the drop indicators
    QMouseEvent mm(QEvent::MouseMove, container_->mapFromGlobal(global_pos), global_pos, Qt::NoButton, Qt::LeftButton,
                   Qt::NoModifier);
    QApplication::sendEvent(container_, &mm);
  }
}

void ChartsScrollArea::resizeEvent(QResizeEvent* event) {
  QScrollArea::resizeEvent(event);
  container_->updateLayout(container_->active_charts_, settings.chart_column_count, true);
}
