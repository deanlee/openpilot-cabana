#pragma once
#include <QStaticText>
#include <QStyledItemDelegate>

enum {
  ColorsRole = Qt::UserRole + 1,
  BytesRole = Qt::UserRole + 2
};

class MessageDelegate : public QStyledItemDelegate {
  Q_OBJECT
public:
  MessageDelegate(QObject *parent, bool multiple_lines = false);
  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  bool multipleLines() const { return multiple_lines; }
  void setMultipleLines(bool v) { multiple_lines = v; }
  QSize sizeForBytes(int n) const;

private:
  std::array<QStaticText, 256> hex_text_table;
  QFontMetrics font_metrics;
  QFont fixed_font;
  QSize byte_size = {};
  bool multiple_lines = false;
  int h_margin, v_margin;
};
