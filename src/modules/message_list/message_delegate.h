#pragma once
#include <QStaticText>
#include <QStyledItemDelegate>

enum class CallerType {
  MessageList,
  HistoryView
};

enum ColumnTypeRole {
  IsHexColumn = Qt::UserRole + 11
};

class MessageDelegate : public QStyledItemDelegate {
  Q_OBJECT
 public:
  MessageDelegate(QObject* parent, CallerType caller_type, bool multiple_lines = false);
  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
  bool multipleLines() const { return multiple_lines; }
  void setMultipleLines(bool v) { multiple_lines = v; }
  QSize sizeForBytes(int n) const;

 private:
  void drawItemText(QPainter* painter, const QStyleOptionViewItem& option,
                    const QModelIndex& index, const QString& text, bool is_selected) const;

  std::array<QStaticText, 256> hex_text_table;
  QFont fixed_font;
  QSize byte_size = {};
  CallerType caller_type_;
  bool multiple_lines = false;
  int h_margin, v_margin;
};
