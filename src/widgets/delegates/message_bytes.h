#pragma once

#include <QStaticText>
#include <QStyledItemDelegate>

#include "dbc/dbc.h"

class MessageBytesDelegate : public QStyledItemDelegate {
public:
  MessageBytesDelegate(QObject *parent);
  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  bool hasSignal(const QModelIndex &index, int dx, int dy, const cabana::Signal *sig) const;
  void drawSignalCell(QPainter* painter, const QStyleOptionViewItem &option, const QModelIndex &index, const cabana::Signal *sig) const;

  QFont small_font, hex_font;
  std::array<QStaticText, 256> hex_text_table;
  std::array<QStaticText, 2> bin_text_table;
};
