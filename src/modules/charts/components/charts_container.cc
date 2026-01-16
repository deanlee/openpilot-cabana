#include "charts_container.h"

#include <QMimeData>
#include <QPainter>
#include <QTimer>

#include "charts_toolbar.h"  // for MAX_COLUMN_COUNT
#include "modules/charts/chart_view.h"
#include "modules/settings/settings.h"

const int CHART_SPACING = 4;

ChartsContainer::ChartsContainer(QWidget* parent) : QWidget(parent) {
  setAcceptDrops(true);
  setBackgroundRole(QPalette::Window);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, CHART_SPACING, 0, CHART_SPACING);
  main_layout->setSpacing(0);

  grid_layout_ = new QGridLayout();
  grid_layout_->setSpacing(CHART_SPACING);
  main_layout->addLayout(grid_layout_);
  main_layout->addStretch(1);
}

void ChartsContainer::dragEnterEvent(QDragEnterEvent* event) {
  if (event->mimeData()->hasFormat(CHART_MIME_TYPE)) {
    event->acceptProposedAction();
    updateDropIndicator(event->pos());
  }
}

void ChartsContainer::dragLeaveEvent(QDragLeaveEvent* event) {
  updateDropIndicator(QPoint());  // Clear indicator
}

void ChartsContainer::dropEvent(QDropEvent* event) {
  if (event->mimeData()->hasFormat(CHART_MIME_TYPE)) {
    auto* chart = qobject_cast<ChartView*>(event->source());
    auto* after = getDropAfter(event->pos());

    if (chart && chart != after) {
      emit chartDropped(chart, after);
    }
    updateDropIndicator(QPoint());
    event->acceptProposedAction();
  }
}

void ChartsContainer::paintEvent(QPaintEvent* ev) {
  if (!drop_indictor_pos.isNull() && !childAt(drop_indictor_pos)) {
    QRect r = geometry();
    r.setHeight(CHART_SPACING);
    if (auto insert_after = getDropAfter(drop_indictor_pos)) {
      r.moveTop(insert_after->geometry().bottom());
    }

    QPainter p(this);
    p.fillRect(r, palette().highlight());
    return;
  }
  QWidget::paintEvent(ev);
}

ChartView* ChartsContainer::getDropAfter(const QPoint& pos) const {
  if (active_charts_.isEmpty()) return nullptr;

  // 1. Precise hit test
  if (auto* child = qobject_cast<ChartView*>(childAt(pos))) {
    if (pos.y() > child->geometry().center().y()) {
      return child;
    }
    // If in top half, return the chart BEFORE this one
    int idx = active_charts_.indexOf(child);
    return (idx > 0) ? active_charts_.at(idx - 1) : nullptr;
  }

  // 2. Proximity fallback (gutters and margins)
  ChartView* last_above = nullptr;
  for (auto* chart : active_charts_) {
    if (pos.y() >= chart->geometry().top()) {
      last_above = chart;
    }
  }
  return last_above;
}

int ChartsContainer::calculateOptimalColumns() const {
  int n = MAX_COLUMN_COUNT;
  for (; n > 1; --n) {
    int required_w = (n * CHART_MIN_WIDTH) + ((n - 1) * grid_layout_->spacing());
    if (required_w <= width()) break;
  }
  return std::min(n, settings.chart_column_count);
}

void ChartsContainer::updateLayout(const QList<ChartView*>& current_charts, int column_count, bool force) {
  if (!force && active_charts_ == current_charts && current_column_count_ == column_count) return;

  active_charts_ = current_charts;
  current_column_count_ = calculateOptimalColumns();
  reflowLayout();
}

void ChartsContainer::reflowLayout() {
  if (active_charts_.isEmpty()) return;

  setUpdatesEnabled(false);

  while (QLayoutItem* item = grid_layout_->takeAt(0)) {
    delete item;
  }

  for (int i = 0; i < active_charts_.size(); ++i) {
    auto* chart = active_charts_[i];
    chart->setVisible(false);
    grid_layout_->addWidget(chart, i / current_column_count_, i % current_column_count_);
    if (chart->chart()->sigs_.empty()) {
      // the chart will be resized after add signal. delay setVisible to reduce flicker.
      QTimer::singleShot(0, chart, [c = chart]() { c->setVisible(true); });
    } else {
      chart->setVisible(true);
    }
  }

  setUpdatesEnabled(true);
}

void ChartsContainer::updateDropIndicator(const QPoint& pt) {
  drop_indictor_pos = pt;
  update();
}

void ChartsContainer::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);

  int new_cols = calculateOptimalColumns();
  if (new_cols != current_column_count_) {
    current_column_count_ = new_cols;
    reflowLayout();
  }
}
