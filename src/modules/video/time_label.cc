#include "time_label.h"

#include <QFontDatabase>
#include <QPainter>

TimeLabel::TimeLabel(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
}
void TimeLabel::setText(const QString& new_text) {
  if (text != new_text) {
    text = new_text;
    update();
  }
}

void TimeLabel::paintEvent(QPaintEvent* event) {
  QPainter p(this);

  const QRect r = rect();
  p.fillRect(r, palette().color(QPalette::Window));
  p.setPen(palette().color(QPalette::WindowText));
  p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, text);
}
