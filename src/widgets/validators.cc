#include "validators.h"

NameValidator::NameValidator(QObject* parent) : QRegExpValidator(QRegExp("^(\\w+)"), parent) {}

QValidator::State NameValidator::validate(QString& input, int& pos) const {
  input.replace(' ', '_');
  return QRegExpValidator::validate(input, pos);
}

DoubleValidator::DoubleValidator(QObject* parent) : QDoubleValidator(parent) {
  // Match locale of QString::toDouble() instead of system
  QLocale locale(QLocale::C);
  locale.setNumberOptions(QLocale::RejectGroupSeparator);
  setLocale(locale);
}
