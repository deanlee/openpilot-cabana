#pragma once

#include <QStaticText>
#include <QStyledItemDelegate>

#include "core/dbc/dbc_message.h"

class MessageBytesDelegate : public QStyledItemDelegate {
 public:
  MessageBytesDelegate(QObject* parent);

 protected:
  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
  void drawSignalCell(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index,
                      const dbc::Signal* sig) const;

  QFont small_font, hex_font;
  std::array<QStaticText, 256> hex_text_table;
  std::array<QStaticText, 2> bin_text_table;
};
