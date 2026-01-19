#include "tiplabel.h"

#include <QApplication>
#include <QStyleOptionFrame>
#include <QStylePainter>
#include <QToolTip>
#include <utility>

#include "modules/settings/settings.h"
#include "utils/util.h"

TipLabel::TipLabel(QWidget* parent) : QLabel(parent, Qt::ToolTip | Qt::FramelessWindowHint) {
  setWindowFlags(windowFlags() | Qt::WindowTransparentForInput | Qt::WindowStaysOnTopHint);

  setAttribute(Qt::WA_ShowWithoutActivating);
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_NoSystemBackground);

  // Modern sans-serif look
  QFont font;
  font.setPointSizeF(9.0);
  setFont(font);

  setTextFormat(Qt::RichText);
  setMargin(8);  // Generous padding for readability

  updateTheme();
}

void TipLabel::showText(const QPoint& pt, const QString& text, QWidget* w, const QRect& rect) {
  if (text.isEmpty()) {
    setVisible(false);
    return;
  }

  setText(text);
  adjustSize();

  // Position logic: 12px offset from cursor
  QPoint tip_pos(pt.x() + 12, rect.top() + 5);
  if (tip_pos.x() + width() >= rect.right()) {
    tip_pos.rx() = pt.x() - width() - 12;
  }

  move(w->mapToGlobal(tip_pos));
  if (!isVisible()) show();
}

void TipLabel::updateTheme() {
  auto pal = palette();
  bool is_dark = utils::isDarkTheme();

  // Neutral professional colors: Deep Grey or Pure White with 85% Alpha
  QColor bg = is_dark ? QColor(30, 30, 30, 215) : QColor(248, 248, 248, 225);
  QColor text = is_dark ? QColor(220, 220, 220) : QColor(50, 50, 50);

  pal.setColor(QPalette::ToolTipBase, bg);
  pal.setColor(QPalette::ToolTipText, text);
  setPalette(pal);
}

void TipLabel::changeEvent(QEvent* e) {
  if (e->type() == QEvent::PaletteChange || e->type() == QEvent::StyleChange) {
    updateTheme();
  }
  QLabel::changeEvent(e);
}

void TipLabel::paintEvent(QPaintEvent* ev) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  // Draw the semi-transparent "Glass" container
  QRectF r = rect();
  r.adjust(0.5, 0.5, -0.5, -0.5);

  QColor bg = palette().color(QPalette::ToolTipBase);
  // Border: 1px subtle line to separate from chart
  QColor border = utils::isDarkTheme() ? QColor(80, 80, 80, 150) : QColor(200, 200, 200, 200);

  p.setBrush(bg);
  p.setPen(QPen(border, 1));
  p.drawRoundedRect(r, 4, 4);

  p.end();

  // Draw the actual HTML/RichText content
  QLabel::paintEvent(ev);
}
