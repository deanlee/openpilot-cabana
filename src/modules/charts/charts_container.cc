#include "charts_container.h"

#include <QMimeData>
#include <QPainter>

#include "chart_view.h"
#include "charts_panel.h"

const int CHART_SPACING = 4;

ChartsContainer::ChartsContainer(ChartsPanel *parent) : charts_widget(parent), QWidget(parent) {
  setAcceptDrops(true);
  setBackgroundRole(QPalette::Window);
  QVBoxLayout *charts_main_layout = new QVBoxLayout(this);
  charts_main_layout->setContentsMargins(0, CHART_SPACING, 0, CHART_SPACING);
  charts_layout = new QGridLayout();
  charts_layout->setSpacing(CHART_SPACING);
  charts_main_layout->addLayout(charts_layout);
  charts_main_layout->addStretch(1);
}

void ChartsContainer::dragEnterEvent(QDragEnterEvent *event) {
  if (event->mimeData()->hasFormat(CHART_MIME_TYPE)) {
    event->acceptProposedAction();
    drawDropIndicator(event->pos());
  }
}

void ChartsContainer::dropEvent(QDropEvent *event) {
  if (event->mimeData()->hasFormat(CHART_MIME_TYPE)) {
    auto w = getDropAfter(event->pos());
    auto chart = qobject_cast<ChartView *>(event->source());
    if (w != chart) {
      for (auto &list: charts_widget->tab_manager_->tab_charts_) {
        list.removeOne(chart);
      }
      int to = w ? charts_widget->currentCharts().indexOf(w) + 1 : 0;
      charts_widget->currentCharts().insert(to, chart);
      charts_widget->updateLayout(true);
      charts_widget->tab_manager_->updateLabels();
      event->acceptProposedAction();
      chart->startAnimation();
    }
    drawDropIndicator({});
  }
}

void ChartsContainer::paintEvent(QPaintEvent *ev) {
  if (charts_widget->currentCharts().isEmpty()) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect r = rect();
    p.fillRect(rect(), palette().color(QPalette::Base));
    QColor text_color = palette().color(QPalette::Disabled, QPalette::Text);

    // Icon
    QPixmap icon = utils::icon("activity", QSize(64, 64), text_color);
    p.drawPixmap(r.center().x() - 32, r.center().y() - 80, icon);

    // Text
    p.setPen(text_color);
    p.setFont(QFont("sans-serif", 12, QFont::Bold));
    p.drawText(r.adjusted(0, 20, 0, 20), Qt::AlignCenter, tr("No Charts Open"));

    p.setFont(QFont("sans-serif", 10));
    p.drawText(r.adjusted(0, 60, 0, 60), Qt::AlignCenter, tr("Select a signal from the list or click [+] to start."));
    return;
  }

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

ChartView *ChartsContainer::getDropAfter(const QPoint &pos) const {
  auto it = std::find_if(charts_widget->currentCharts().crbegin(), charts_widget->currentCharts().crend(), [&pos](auto c) {
    auto area = c->geometry();
    return pos.x() >= area.left() && pos.x() <= area.right() && pos.y() >= area.bottom();
  });
  return it == charts_widget->currentCharts().crend() ? nullptr : *it;
}
