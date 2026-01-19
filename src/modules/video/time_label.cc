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
  static const QColor bg = palette().color(QPalette::Window);
  static const QColor fg = palette().color(QPalette::WindowText);

  QPainter p(this);

  const QRect r = rect();
  p.fillRect(r, bg);
  p.setPen(fg);
  p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, text);
}
