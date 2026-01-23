#include "debounced_line_edit.h"

DebouncedLineEdit::DebouncedLineEdit(QWidget* parent, int delay_ms) : QLineEdit(parent) {
  timer = new QTimer(this);
  timer->setSingleShot(true);
  timer->setInterval(delay_ms);

  connect(this, &QLineEdit::textEdited, timer, qOverload<>(&QTimer::start));
  connect(timer, &QTimer::timeout, this, [this]() {
    emit debouncedTextEdited(this->text());
  });

  // High performance: if the user clears the text, trigger immediately
  connect(this, &QLineEdit::textChanged, this, [this](const QString& text) {
    if (text.isEmpty()) {
      timer->stop();
      emit debouncedTextEdited(text);
    }
  });
}

void DebouncedLineEdit::setDelay(int ms) {
  timer->setInterval(ms);
}
