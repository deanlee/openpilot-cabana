#include "charts_empty_view.h"

#include <QPainter>

#include "utils/util.h"

ChartsEmptyView::ChartsEmptyView(QWidget* parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ChartsEmptyView::paintEvent(QPaintEvent* ev) {
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
