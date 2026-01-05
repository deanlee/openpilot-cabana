#pragma once
#include <QPainter>
#include <QStyledItemDelegate>

#include "dbc/dbc.h"
#include "models/signal_tree.h"

class SignalTreeDelegate : public QStyledItemDelegate {
public:

  const int BTN_WIDTH = 24;
  const int BTN_SPACING = 4;

  SignalTreeDelegate(QObject *parent);
  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;
  bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option, const QModelIndex &index) override;
  void drawButtons(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index, SignalTreeModel::Item *item) const;
  bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option, const QModelIndex &index) override;
  void clearHoverState();
  int getButtonsWidth() const { return BTN_WIDTH * 2 + BTN_SPACING; }

  QValidator *name_validator, *double_validator, *node_validator;
  QFont label_font, minmax_font;
  const int color_label_width = 18;
  mutable QSize button_size;
  mutable QModelIndex hoverIndex;
  mutable int hoverButton = -1; // -1: none, 0: plot, 1: remove

private:
  QRect getButtonRect(const QRect &columnRect, int buttonIndex) const;
  int buttonAt(const QPoint& pos, const QRect& rect) const;
};
