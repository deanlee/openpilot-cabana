#pragma once

#include <QPixmap>
#include <QStyledItemDelegate>

enum class CallerType {
  MessageList,
  HistoryView
};

enum ColumnTypeRole {
  IsHexColumn = Qt::UserRole + 11,
  MsgActiveRole,
};

class MessageDelegate : public QStyledItemDelegate {
  Q_OBJECT
 public:
  MessageDelegate(QObject* parent, CallerType caller_type);
  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
  QSize sizeForBytes(int n) const;

 private:
  void updatePixmapCache(const QPalette& palette) const;
  void drawItemText(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx, bool sel, bool active) const;
  void drawHexData(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx, bool sel, bool active) const;

  QFont fixed_font;
  QSize byte_size = {};
  CallerType caller_type_;
  int h_margin, v_margin;

  mutable QPixmap hex_pixmap_table[256][3];
  mutable QPalette cached_palette;
  enum RenderState { StateNormal = 0, StateSelected = 1, StateDisabled = 2 };
};
