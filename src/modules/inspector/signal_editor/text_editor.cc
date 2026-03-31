#include "text_editor.h"

#include <QVBoxLayout>

DBCEditor::DBCEditor(QWidget* parent) : QWidget(parent) {
  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  text_edit = new QPlainTextEdit(this);
  layout->addWidget(text_edit);

  connect(text_edit, &QPlainTextEdit::textChanged, this, &DBCEditor::onTextChanged);
}

void DBCEditor::setSignalText(const QString& text) {
  text_edit->setPlainText(text);
}

QString DBCEditor::getSignalText() const {
  return text_edit->toPlainText();
}

void DBCEditor::onTextChanged() {
  emit signalTextChanged(text_edit->toPlainText());
}
