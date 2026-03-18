#pragma once

#include <QPainter>
#include <QPersistentModelIndex>
#include <QStyledItemDelegate>

#include "core/dbc/dbc_message.h"
#include "signal_tree_model.h"

class SignalTreeDelegate : public QStyledItemDelegate {
  Q_OBJECT
 public:
  // Layout constants
  static constexpr int kBtnSize = 22;
  static constexpr int kBtnSpacing = 4;
  static constexpr int kPadding = 6;
  static constexpr int kColorLabelW = 18;

  explicit SignalTreeDelegate(QObject* parent);
  void clearHoverState();
  int nameColumnWidth(const dbc::Signal* sig) const;
  int getButtonsWidth() const { return (2 * kBtnSize) + kBtnSpacing + kPadding; }

  // QStyledItemDelegate overrides
  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
  QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& idx) const override;
  void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;
  bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                   const QModelIndex& index) override;
  bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option,
                 const QModelIndex& index) override;

 signals:
  void removeRequested(const dbc::Signal*);
  void plotRequested(const dbc::Signal*, bool show, bool merge);

 private:
  // Painting helpers
  void paintSignalRow(QPainter* p, const QStyleOptionViewItem& opt, SignalItem* item, const QModelIndex& idx) const;
  void paintSignalNameColumn(QPainter* p, QRect rect, const QStyleOptionViewItem& opt, SignalItem* item) const;
  void paintSignalValueColumn(QPainter* p, QRect rect, const QStyleOptionViewItem& opt, SignalItem* item,
                              const QModelIndex& idx) const;
  void paintButtons(QPainter* p, const QStyleOptionViewItem& opt, SignalItem* item, const QModelIndex& idx) const;
  void paintPropertyRow(QPainter* p, const QStyleOptionViewItem& opt, PropertyItem* item, const QModelIndex& idx) const;

  // Button handling
  QRect buttonRect(const QRect& colRect, int btnIdx) const;
  int buttonAt(const QPoint& pos, const QRect& rect) const;

  // Validators
  QValidator* nameValidator_ = nullptr;
  QValidator* doubleValidator_ = nullptr;
  QValidator* nodeValidator_ = nullptr;

  // Fonts
  QFont labelFont_;
  QFont minmaxFont_;
  QFont valueFont_;

  // Colors
  QColor signalTextColor_;

  // Hover state
  mutable QPersistentModelIndex hoverIndex_;
  mutable int hoverButton_ = -1;  // -1: none, 0: remove, 1: plot
};
