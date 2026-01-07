#pragma once
#include <QPainter>
#include <QStyledItemDelegate>

#include "dbc/dbc.h"
#include "models/signal_tree.h"

class SignalTreeDelegate : public QStyledItemDelegate {
public:

  // Layout Constants
  const int kBtnSize = 22;
  const int kBtnSpacing = 4;
  const int kPadding = 6;
  const int kColorLabelW = 18;
  const int kValueWidth = 65;

  SignalTreeDelegate(QObject *parent);
  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;
  bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option, const QModelIndex &index) override;
  bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option, const QModelIndex &index) override;
  void clearHoverState();
  int nameColumnWidth(const cabana::Signal* sig) const;
  inline int signalRowHeight() const { return kBtnSize + 4; }
  inline int getButtonsWidth() const {
    // Calculate total space taken by buttons area on the right
    return (2 * kBtnSize) + kBtnSpacing + kPadding;
  }

  QValidator *name_validator = nullptr;
  QValidator *double_validator = nullptr;
  QValidator *node_validator = nullptr;
  QFont label_font, minmax_font;
  const int color_label_width = 18;
  mutable QSize button_size;
  mutable QModelIndex hoverIndex;
  mutable int hoverButton = -1; // -1: none, 0: plot, 1: remove

private:
  QRect getButtonRect(const QRect &columnRect, int buttonIndex) const;
  int buttonAt(const QPoint& pos, const QRect& rect) const;
  void drawNameColumn(QPainter* p, QRect r, const QStyleOptionViewItem& opt, SignalTreeModel::Item* item, const QModelIndex& idx) const;
  void drawDataColumn(QPainter* p, QRect r, const QStyleOptionViewItem& opt, SignalTreeModel::Item* item, const QModelIndex& idx) const;
  void drawButtons(QPainter *painter, const QStyleOptionViewItem &option, SignalTreeModel::Item *item, const QModelIndex &idx) const;
};
