#pragma once

#include <optional>
#include <vector>

#include <QByteArray>
#include <QColor>
#include <QPainter>
#include <QStaticText>
#include <QStringBuilder>

namespace utils {

QPixmap icon(const QString &id, QSize size = QSize(24, 24), std::optional<QColor> color = std::nullopt);
bool isDarkTheme();
void setTheme(int theme);
QString formatSeconds(double sec, bool include_milliseconds = false, bool absolute_time = false);
inline void drawStaticText(QPainter *p, const QRect &r, const QStaticText &text) {
  auto size = (r.size() - text.size()) / 2;
  p->drawStaticText(r.left() + size.width(), r.top() + size.height(), text);
}
inline QString toHex(const std::vector<uint8_t> &dat, char separator = '\0') {
  return QByteArray::fromRawData((const char *)dat.data(), dat.size()).toHex(separator).toUpper();
}
QString doubleToString(double value);

}

void initApp(int argc, char *argv[], bool disable_hidpi = true);
