#include "welcome_widget.h"

#include <QPainter>

void WelcomeWidget::paintEvent(QPaintEvent* event) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  const QRect r = rect();
  const int cx = r.center().x();
  const int cy = r.center().y();
  const QColor text_color = palette().color(QPalette::Disabled, QPalette::Text);

  // 1. Logo & Instruction
  p.setPen(text_color);
  p.setFont(QFont("sans-serif", 40, QFont::Light));
  p.drawText(r.adjusted(0, 0, 0, -140), Qt::AlignCenter, "CABANA");

  p.setFont(QFont("sans-serif", 12));
  p.drawText(r.adjusted(0, 0, 0, 20), Qt::AlignCenter, tr("<-Select a message to view details"));

  // 2. Shortcuts
  const QList<std::pair<QString, QString>> shortcuts = {
      {tr("Play / Pause"), "Space"},
      {tr("Help"), "F1"},
      {tr("WhatsThis"), "Shift+F1"}};

  p.setFont(QFont("sans-serif", 10));
  for (int i = 0; i < shortcuts.size(); ++i) {
    int y = cy + 60 + (i * 35);

    // Label (Right aligned)
    p.setPen(palette().color(QPalette::Disabled, QPalette::WindowText));
    p.drawText(cx - 160, y, 140, 25, Qt::AlignRight | Qt::AlignVCenter, shortcuts[i].first);

    // Keycap (Left aligned)
    QRect key_rect(cx - 5, y, 80, 25);
    p.drawRoundedRect(key_rect, 4, 4);

    p.setPen(text_color);
    p.drawText(key_rect, Qt::AlignCenter, shortcuts[i].second);
  }
}
