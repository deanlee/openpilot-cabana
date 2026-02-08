#include "charts_container.h"

#include <QMimeData>
#include <QPainter>
#include <QTimer>

#include "charts_scroll_area.h"
#include "charts_toolbar.h"  // for MAX_COLUMN_COUNT
#include "modules/charts/chart_view.h"
#include "modules/settings/settings.h"

const int CHART_SPACING = 4;

ChartsContainer::ChartsContainer(QWidget* parent) : QWidget(parent) {
  setAcceptDrops(true);
  setBackgroundRole(QPalette::Window);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  setMouseTracking(true);

  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(CHART_SPACING, CHART_SPACING, CHART_SPACING, CHART_SPACING);
  main_layout->setSpacing(0);

  grid_layout_ = new QGridLayout();
  grid_layout_->setSpacing(CHART_SPACING);
  main_layout->addLayout(grid_layout_);
  main_layout->addStretch(1);
}

int ChartsContainer::calculateOptimalColumns() const {
  int visible_width = width();
  QWidget* viewport = parentWidget();
  if (viewport) {
    visible_width = viewport->width();
  }

  int n = MAX_COLUMN_COUNT;
  for (; n > 1; --n) {
    int required_w = (n * CHART_MIN_WIDTH) + ((n - 1) * grid_layout_->spacing());
    if (required_w <= visible_width) break;
  }
  return std::min(n, settings.chart_column_count);
}

void ChartsContainer::updateLayout(const QList<ChartView*>& current_charts, int column_count, bool force) {
  int optimal_columns = calculateOptimalColumns();
  if (!force && active_charts_ == current_charts && current_column_count_ == optimal_columns) return;

  active_charts_ = current_charts;
  current_column_count_ = optimal_columns;
  reflowLayout();
}

void ChartsContainer::reflowLayout() {
  setUpdatesEnabled(false);

  while (QLayoutItem* item = grid_layout_->takeAt(0)) {
    item->widget()->setVisible(false);
    delete item;
  }

  for (int i = 0; i < active_charts_.size(); ++i) {
    auto* chart = active_charts_[i];
    chart->setVisible(false);
    grid_layout_->addWidget(chart, i / current_column_count_, i % current_column_count_);
    if (chart->empty()) {
      // the chart will be resized after add signal. delay setVisible to reduce flicker.
      QTimer::singleShot(0, chart, [c = chart]() { c->setVisible(true); });
    } else {
      chart->setVisible(true);
    }
  }

  setUpdatesEnabled(true);
}

void ChartsContainer::handleDragInteraction(const QPoint& pos) {
  // Start auto-scroll via parent hierarchy
  if (auto* sa = qobject_cast<ChartsScrollArea*>(parentWidget()->parentWidget())) {
    sa->startAutoScroll();
  }

  ChartView* best_match = nullptr;
  for (auto* chart : active_charts_) {
    // Find chart including the gap area around it
    if (chart->geometry().adjusted(0, -CHART_SPACING, 0, CHART_SPACING).contains(pos)) {
      best_match = chart;
      break;
    }
  }

  if (best_match) {
    active_target = best_match;
    double rel_y = static_cast<double>(pos.y() - active_target->y()) / active_target->height();
    if (rel_y < 0.2)
      drop_mode = DropMode::InsertBefore;
    else if (rel_y > 0.8)
      drop_mode = DropMode::InsertAfter;
    else
      drop_mode = DropMode::Merge;
  } else {
    active_target = nullptr;
    drop_mode = DropMode::None;
  }
  update();
}

void ChartsContainer::dragEnterEvent(QDragEnterEvent* event) {
  if (event->mimeData()->hasFormat(CHART_MIME_TYPE)) {
    event->acceptProposedAction();
  }
}

void ChartsContainer::dragMoveEvent(QDragMoveEvent* e) {
  if (e->mimeData()->hasFormat(CHART_MIME_TYPE)) {
    e->acceptProposedAction();
    handleDragInteraction(e->position().toPoint());
  }
}

void ChartsContainer::dropEvent(QDropEvent* event) {
  if (event->mimeData()->hasFormat(CHART_MIME_TYPE)) {
    if (active_target) {
      handleDrop(active_target, drop_mode, event);
    }
  }
  resetDragState();
}

void ChartsContainer::dragLeaveEvent(QDragLeaveEvent* event) {
  resetDragState();
  event->accept();
}

bool ChartsContainer::eventFilter(QObject* obj, QEvent* event) {
  auto* vp = qobject_cast<QWidget*>(obj);
  if (!vp) return false;

  if (event->type() == QEvent::DragMove || event->type() == QEvent::DragEnter) {
    auto* dev = static_cast<QDropEvent*>(event);
    if (dev->mimeData()->hasFormat(CHART_MIME_TYPE)) {
      dev->acceptProposedAction();
      handleDragInteraction(mapFromGlobal(vp->mapToGlobal(dev->position())).toPoint());
      return true;
    }
  }

  if (event->type() == QEvent::Drop) {
    auto* dev = static_cast<QDropEvent*>(event);
    if (dev->mimeData()->hasFormat(CHART_MIME_TYPE)) {
      handleDrop(active_target, drop_mode, dev);
      resetDragState();
      return true;
    }
  }

  return QObject::eventFilter(obj, event);
}

void ChartsContainer::resetDragState() {
  if (!active_target && drop_mode == DropMode::None) return;

  active_target = nullptr;
  drop_mode = DropMode::None;
  if (auto* sa = qobject_cast<ChartsScrollArea*>(parentWidget()->parentWidget())) {
    sa->stopAutoScroll();
  }
  update();
}

void ChartsContainer::handleDrop(ChartView* target, DropMode mode, QDropEvent* event) {
  auto* source = qobject_cast<ChartView*>(event->source());
  if (source && target && source != target) {
    emit chartDropped(source, target, mode);
  }
}

void ChartsContainer::paintEvent(QPaintEvent* ev) {
  QWidget::paintEvent(ev);
  if (!active_target || drop_mode == DropMode::None) return;

  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  QColor highlight = palette().color(QPalette::Highlight);
  QRect geo = active_target->geometry();

  if (drop_mode == DropMode::Merge) {
    p.setPen(QPen(highlight, 2, Qt::DashLine));
    p.setBrush(QColor(highlight.red(), highlight.green(), highlight.blue(), 60));
    p.drawRoundedRect(geo.adjusted(-2, -2, 2, 2), 4, 4);
  } else {
    int y = (drop_mode == DropMode::InsertAfter) ? geo.bottom() : geo.top();
    p.setPen(QPen(highlight, 4));
    p.drawLine(10, y, width() - 10, y);
    p.setBrush(highlight);
    p.drawEllipse(QPoint(10, y), 4, 4);
    p.drawEllipse(QPoint(width() - 10, y), 4, 4);
  }
}

void ChartsContainer::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  updateLayout(active_charts_, settings.chart_column_count, false);
}
