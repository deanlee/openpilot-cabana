#pragma once

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QSpinBox>
#include <QTextEdit>

#include "dbc/dbc_message.h"

class MessageEdit : public QDialog {
public:
  MessageEdit(const MessageId &msg_id, const QString &title, int size, QWidget *parent);
  void validateName(const QString &text);

  MessageId msg_id;
  QString original_name;
  QDialogButtonBox *btn_box;
  QLineEdit *name_edit;
  QLineEdit *node;
  QTextEdit *comment_edit;
  QLabel *error_label;
  QSpinBox *size_spin;
};
