#pragma once
#include <QDialog>
#include <QTableWidget>
#include "dbc/dbc_message.h"
#include <QStyledItemDelegate>

class ValueTableEditor : public QDialog {
 public:
  ValueTableEditor(const ValueDescription& descriptions, QWidget* parent);
  ValueDescription val_desc;

 private:
  struct Delegate : public QStyledItemDelegate {
    Delegate(QWidget* parent) : QStyledItemDelegate(parent) {}
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
  };

  void save();
  QTableWidget* table;
};
