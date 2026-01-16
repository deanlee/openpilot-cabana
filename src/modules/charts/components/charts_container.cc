#include "charts_container.h"

#include <QMimeData>
#include <QPainter>
#include <QTimer>

#include "charts_toolbar.h"  // for MAX_COLUMN_COUNT
#include "modules/charts/chart_view.h"

const int CHART_SPACING = 4;

ChartsContainer::ChartsContainer(QWidget* parent) : QWidget(parent) {
  setAcceptDrops(true);
  setBackgroundRole(QPalette::Window);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

  QVBoxLayout* main_layout = new QVBoxLayout(this);
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
  updateDropIndicator({}); // Clear indicator
}

void ChartsContainer::dropEvent(QDropEvent* event) {
  if (event->mimeData()->hasFormat(CHART_MIME_TYPE)) {
    auto chart = qobject_cast<ChartView*>(event->source());
    auto after = getDropAfter(event->pos());

    if (chart && chart != after) {
      emit chartDropped(chart, after);
    }
    updateDropIndicator({});
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
  const int count = grid_layout_->count();
  if (count == 0) return nullptr;

  if (auto* child = qobject_cast<ChartView*>(childAt(pos))) {
    return (pos.y() > child->geometry().center().y()) ? child : nullptr;
  }

  ChartView* last_above = nullptr;
  for (int i = 0; i < count; ++i) {
    if (auto* w = qobject_cast<ChartView*>(grid_layout_->itemAt(i)->widget())) {
      if (pos.y() >= w->geometry().top()) {
        last_above = w;
      }
    }
  }
  return last_above;
}

void ChartsContainer::updateLayout(const QList<ChartView*>& current_charts, int column_count, bool force) {
  int n = MAX_COLUMN_COUNT;
  for (; n > 1; --n) {
    if ((n * CHART_MIN_WIDTH + (n - 1) * grid_layout_->horizontalSpacing()) < grid_layout_->geometry().width()) break;
  }

  // bool show_column_cb = n > 1;
  // columns_action->setVisible(show_column_cb);

  n = std::min(column_count, n);
  if (force || current_charts.size() != grid_layout_->count() || n != current_column_count_) {
    current_column_count_ = n;
    setUpdatesEnabled(false);

    while (QLayoutItem* item = grid_layout_->takeAt(0)) {
      if (item->widget()) item->widget()->hide();
      delete item;
    }

    // Grid placement logic
    for (int i = 0; i < current_charts.size(); ++i) {
      auto *chart = current_charts[i];
      chart->setVisible(false);
      grid_layout_->addWidget(chart, i / n, i % n);
      if (chart->chart()->sigs_.empty()) {
        // the chart will be resized after add signal. delay setVisible to reduce flicker.
        QTimer::singleShot(0, chart, [c = chart]() { c->setVisible(true); });
      } else {
        chart->setVisible(true);
      }
    }

    setUpdatesEnabled(true);
  }
}

void ChartsContainer::updateDropIndicator(const QPoint& pt) {
  drop_indictor_pos = pt;
  update();
}
