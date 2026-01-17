#pragma once

#include <QComboBox>
#include <QDialog>
#include <QListWidget>

#include "core/dbc/dbc_manager.h"

class QDialogButtonBox;

class SignalPicker : public QDialog {
public:
  struct ListItem : public QListWidgetItem {
    ListItem(const MessageId &msg_id, const dbc::Signal *sig, QListWidget *parent) : msg_id(msg_id), sig(sig), QListWidgetItem(parent) {}
    MessageId msg_id;
    const dbc::Signal *sig;
  };

  SignalPicker(QString title, QWidget *parent);
  QList<ListItem *> seletedItems();
  inline void addSelected(const MessageId &id, const dbc::Signal *sig) { addItemToList(selected_list, id, sig, true); }

private:
  void setupConnections();
  void updateAvailableList(int index);
  void addItemToList(QListWidget *parent, const MessageId id, const dbc::Signal *sig, bool show_msg_name = false);
  void add(QListWidgetItem *item);
  void remove(QListWidgetItem *item);

  QComboBox *msgs_combo;
  QListWidget *available_list;
  QListWidget *selected_list;

  QPushButton *add_btn;
  QPushButton *remove_btn;
  QDialogButtonBox *button_box;

  friend class ChattView;
};
