#pragma once

#include <vector>

#include <QByteArray>
#include <QDoubleValidator>
#include <QPainter>
#include <QRegExpValidator>
#include <QStaticText>
#include <QStringBuilder>

class NameValidator : public QRegExpValidator {
  Q_OBJECT
public:
  NameValidator(QObject *parent=nullptr);
  QValidator::State validate(QString &input, int &pos) const override;
};

class DoubleValidator : public QDoubleValidator {
  Q_OBJECT
public:
  DoubleValidator(QObject *parent = nullptr);
};

namespace utils {

QPixmap icon(const QString &id, QSize size = QSize(24, 24));
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

}

void initApp(int argc, char *argv[], bool disable_hidpi = true);
