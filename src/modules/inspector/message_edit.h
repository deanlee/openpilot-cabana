#pragma once

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTextEdit>

#include "core/dbc/dbc_message.h"

class MessageEdit : public QDialog {
 public:
  MessageEdit(const MessageId& msg_id, const QString& title, int size, QWidget* parent);

  QLineEdit* name_edit;
  QLineEdit* node;
  QTextEdit* comment_edit;
  QSpinBox* size_spin;

 private:
  void validateName(const QString& text);

  MessageId msg_id;
  QString original_name;
  QDialogButtonBox* btn_box;
  QLabel* error_label;
};
