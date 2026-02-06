#pragma once

#include <QByteArray>
#include <QColor>
#include <QPainter>
#include <QStaticText>
#include <QStringBuilder>
#include <optional>
#include <vector>

namespace utils {

QPixmap icon(const QString& id, QSize size = QSize(24, 24), std::optional<QColor> color = std::nullopt);
QString doubleToString(double value, int precision = std::numeric_limits<double>::max_digits10);
int num_decimals(double num);
bool isDarkTheme();
void setTheme(int theme);
QString formatSeconds(double sec, bool include_milliseconds = false, bool absolute_time = false);
inline void drawStaticText(QPainter* p, const QRect& r, const QStaticText& text) {
  auto size = (r.size() - text.size()) / 2;
  p->drawStaticText(r.left() + size.width(), r.top() + size.height(), text);
}
inline QString toHex(const uint8_t* dat, int size, char separator = '\0') {
  return QByteArray::fromRawData((const char*)dat, size).toHex(separator).toUpper();
}

}  // namespace utils

void initApp(int argc, char* argv[], bool disable_hidpi = true);
