#include "message_edit.h"

#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>

#include "core/dbc/dbc_manager.h"
#include "utils/util.h"
#include "widgets/validators.h"
#include "core/streams/message_state.h"

MessageEdit::MessageEdit(const MessageId& msg_id, const QString& title, int size, QWidget* parent)
    : original_name(title), msg_id(msg_id), QDialog(parent) {
  setWindowTitle(tr("Edit message: %1").arg(msg_id.toString()));
  QFormLayout* form_layout = new QFormLayout(this);

  form_layout->addRow("", error_label = new QLabel);
  error_label->setVisible(false);
  form_layout->addRow(tr("Name"), name_edit = new QLineEdit(title, this));
  name_edit->setValidator(new NameValidator(name_edit));

  form_layout->addRow(tr("Size"), size_spin = new QSpinBox(this));
  size_spin->setRange(1, MAX_CAN_LEN);
  size_spin->setValue(size);

  form_layout->addRow(tr("Node"), node = new QLineEdit(this));
  node->setValidator(new NameValidator(name_edit));
  form_layout->addRow(tr("Comment"), comment_edit = new QTextEdit(this));
  form_layout->addRow(btn_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel));

  if (auto msg = GetDBC()->msg(msg_id)) {
    node->setText(msg->transmitter);
    comment_edit->setText(msg->comment);
  }
  validateName(name_edit->text());
  setFixedWidth(parent->width() * 0.9);
  connect(name_edit, &QLineEdit::textEdited, this, &MessageEdit::validateName);
  connect(btn_box, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(btn_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void MessageEdit::validateName(const QString& text) {
  bool valid = text.compare(UNDEFINED, Qt::CaseInsensitive) != 0;
  error_label->setVisible(false);
  if (!text.isEmpty() && valid && text != original_name) {
    valid = GetDBC()->msg(msg_id.source, text) == nullptr;
    if (!valid) {
      error_label->setText(tr("Name already exists"));
      error_label->setVisible(true);
    }
  }
  btn_box->button(QDialogButtonBox::Ok)->setEnabled(valid);
}
