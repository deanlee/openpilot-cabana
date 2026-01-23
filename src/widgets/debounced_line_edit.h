#pragma once

#include <QLineEdit>
#include <QTimer>

class DebouncedLineEdit : public QLineEdit {
  Q_OBJECT
public:
  explicit DebouncedLineEdit(QWidget* parent = nullptr, int delay_ms = 300);
  void setDelay(int ms);

signals:
  void debouncedTextEdited(const QString &text);

private:
  QTimer *timer;
};
