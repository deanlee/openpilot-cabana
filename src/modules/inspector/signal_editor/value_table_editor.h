#pragma once
#include <QDialog>
#include <QStyledItemDelegate>
#include <QTableWidget>

#include "core/dbc/dbc_message.h"

class QDialogButtonBox;
class QPushButton;

class ValueTableEditor : public QDialog {
 public:
  ValueTableEditor(const ValueTable& descriptions, QWidget* parent);
  ValueTable value_table;

 private:
  void setupConnections();
  struct Delegate : public QStyledItemDelegate {
    Delegate(QWidget* parent) : QStyledItemDelegate(parent) {}
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
  };

  void save();

  QTableWidget* table;
  QDialogButtonBox *btn_box;
  QPushButton *add_btn;
  QPushButton *remove_btn;
};
