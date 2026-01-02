#pragma once
#include <QDialog>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTableWidget>

#include "dbc/dbc.h"

class ValueDescriptionDlg : public QDialog {
public:
  ValueDescriptionDlg(const ValueDescription &descriptions, QWidget *parent);
  ValueDescription val_desc;

private:
  struct Delegate : public QStyledItemDelegate {
    Delegate(QWidget *parent) : QStyledItemDelegate(parent) {}
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  };

  void save();
  QTableWidget *table;
};

class SignalTreeDelegate : public QStyledItemDelegate {
public:
  SignalTreeDelegate(QObject *parent);
  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;

  QValidator *name_validator, *double_validator, *node_validator;
  QFont label_font, minmax_font;
  const int color_label_width = 18;
  mutable QSize button_size;
};
