#pragma once
#include <QDialog>
#include <QStyledItemDelegate>
#include <QTableWidget>

#include "core/dbc/dbc_message.h"

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
