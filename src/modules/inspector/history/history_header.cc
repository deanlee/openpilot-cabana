#include "history_header.h"

#include <QPainter>

#include "utils/util.h"

QSize HistoryHeader::sectionSizeFromContents(int logicalIndex) const {
  static const QSize time_col_size = fontMetrics().size(0, "000000.000") + QSize(20, 10);
  if (logicalIndex == 0) return time_col_size;

  QString text = model()->headerData(logicalIndex, orientation(), Qt::DisplayRole).toString();
  int w = fontMetrics().horizontalAdvance(text) + 20;
  return QSize(qBound(100, w, 300), fontMetrics().height() + 10);
}

void HistoryHeader::paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const {
  painter->save();

  QVariant bg = model()->headerData(logicalIndex, orientation(), Qt::BackgroundRole);
  if (bg.isValid()) painter->fillRect(rect, bg.value<QColor>());

  QString text = model()->headerData(logicalIndex, orientation(), Qt::DisplayRole).toString();
  painter->setPen(palette().color(utils::isDarkTheme() ? QPalette::BrightText : QPalette::Text));

  QRect text_rect = rect.adjusted(5, 0, -5, 0);
  QString elided = fontMetrics().elidedText(text, Qt::ElideMiddle, text_rect.width());
  painter->drawText(text_rect, Qt::AlignCenter | Qt::TextSingleLine, elided);

  painter->restore();
}
