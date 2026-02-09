#pragma once

#include <array>

#include <QPixmap>
#include <QStyledItemDelegate>

enum class CallerType { MessageList, HistoryView };

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
  enum RenderState { StateNormal = 0, StateSelected = 1, StateDisabled = 2, StateCount = 3 };
  static constexpr int kGapWidth = 8;  // Extra pixels added every 8 bytes

  void updatePixmapCache(const QPalette& palette) const;
  void drawItemText(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx, bool sel, bool active) const;
  void drawHexData(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx, bool sel, bool active) const;

  QFont fixed_font;
  QSize byte_size = {};
  CallerType caller_type_;
  int h_margin, v_margin;

  mutable std::array<std::array<QPixmap, StateCount>, 256> hex_pixmap_table;
  mutable qint64 cached_palette_key = 0;
};
