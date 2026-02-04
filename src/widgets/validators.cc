#include "validators.h"

#include <limits>

NameValidator::NameValidator(QObject* parent) : QRegularExpressionValidator(QRegularExpression("^(\\w+)"), parent) {}

QValidator::State NameValidator::validate(QString& input, int& pos) const {
  input.replace(' ', '_');
  return QRegularExpressionValidator::validate(input, pos);
}

DoubleValidator::DoubleValidator(QObject* parent) : QDoubleValidator(parent) {
  // Use C locale (dot as decimal) to match DBC/JSON standards
  QLocale locale(QLocale::C);
  locale.setNumberOptions(QLocale::RejectGroupSeparator);
  setLocale(locale);

  // StandardNotation = Fixed point. (ScientificNotation is the other option)
  setNotation(QDoubleValidator::StandardNotation);

  // Set precision to match our doubleToString helper
  setDecimals(std::numeric_limits<double>::max_digits10);
}

QValidator::State DoubleValidator::validate(QString& input, int& pos) const {
  // Hard-block 'e' or 'E' from being typed at all
  if (input.contains('e', Qt::CaseInsensitive)) {
    return QValidator::Invalid;
  }

  return QDoubleValidator::validate(input, pos);
}