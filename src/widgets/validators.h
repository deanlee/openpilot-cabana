#pragma once

#include <QDoubleValidator>
#include <QRegExpValidator>

class NameValidator : public QRegExpValidator {
  Q_OBJECT
 public:
  NameValidator(QObject* parent = nullptr);
  QValidator::State validate(QString& input, int& pos) const override;
};

class DoubleValidator : public QDoubleValidator {
  Q_OBJECT
 public:
  DoubleValidator(QObject* parent = nullptr);
};
