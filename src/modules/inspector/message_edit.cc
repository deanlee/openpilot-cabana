#include "message_edit.h"

#include <QCompleter>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <set>

#include "core/dbc/dbc_manager.h"
#include "core/streams/message_state.h"
#include "utils/util.h"
#include "widgets/validators.h"

MessageEdit::MessageEdit(const MessageId& msg_id, const QString& title, int size, QWidget* parent)
    : QDialog(parent), msg_id(msg_id), original_name(title) {
  setWindowTitle(tr("Edit message: %1").arg(msg_id.toString()));
  QFormLayout* form_layout = new QFormLayout(this);

  error_label = new QLabel(this);
  error_label->setStyleSheet("color: red;");
  error_label->setVisible(false);
  form_layout->addWidget(error_label);
  form_layout->addRow(tr("Name"), name_edit = new QLineEdit(title, this));
  name_edit->setValidator(new NameValidator(name_edit));

  form_layout->addRow(tr("Size"), size_spin = new QSpinBox(this));
  size_spin->setRange(1, MAX_CAN_LEN);
  size_spin->setValue(size);

  node_edit = new QComboBox(this);
  node_edit->setEditable(true);
  node_edit->setInsertPolicy(QComboBox::NoInsert);
  node_edit->completer()->setCompletionMode(QCompleter::PopupCompletion);
  std::set<QString> nodes;
  for (const auto& [_, msg] : GetDBC()->getMessages(msg_id.source)) {
    if (!msg.transmitter.isEmpty()) {
      nodes.insert(msg.transmitter);
    }
  }
  for (const auto& node : nodes) {
    node_edit->addItem(node);
  }
  form_layout->addRow(tr("Node"), node_edit);

  form_layout->addRow(tr("Comment"), comment_edit = new QTextEdit(this));
  form_layout->addRow(btn_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel));

  if (auto msg = GetDBC()->msg(msg_id)) {
    node_edit->setCurrentText(msg->transmitter);
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
  QString error;
  if (!text.isEmpty() && valid && text != original_name) {
    if (GetDBC()->msg(msg_id.source, text) != nullptr) {
      error = tr("A message named %1 already exists on this bus.").arg(text);
      valid = false;
    }
  }
  error_label->setText(error);
  error_label->setVisible(!error.isEmpty());
  btn_box->button(QDialogButtonBox::Ok)->setEnabled(valid);
}
