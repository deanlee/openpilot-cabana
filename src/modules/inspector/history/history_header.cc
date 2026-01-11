#include "history_header.h"

#include <QPainter>

#include "utils/util.h"

QSize HistoryHeader::sectionSizeFromContents(int logicalIndex) const {
  static const QSize time_col_size = fontMetrics().size(Qt::TextSingleLine, "000000.000") + QSize(10, 6);
  if (logicalIndex == 0) {
    return time_col_size;
  } else {
    int default_size = qMax(100, (rect().width() - time_col_size.width()) / (model()->columnCount() - 1));
    QString text = model()->headerData(logicalIndex, this->orientation(), Qt::DisplayRole).toString();
    const QRect rect = fontMetrics().boundingRect({0, 0, default_size, 2000}, defaultAlignment(), text.replace(QChar('_'), ' '));
    QSize size = rect.size() + QSize{10, 6};
    return QSize{qMax(size.width(), default_size), size.height()};
  }
}

void HistoryHeader::paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const {
  auto bg_role = model()->headerData(logicalIndex, Qt::Horizontal, Qt::BackgroundRole);
  if (bg_role.isValid()) {
    painter->fillRect(rect, bg_role.value<QBrush>());
  }
  QString text = model()->headerData(logicalIndex, Qt::Horizontal, Qt::DisplayRole).toString();
  painter->setPen(palette().color(utils::isDarkTheme() ? QPalette::BrightText : QPalette::Text));
  painter->drawText(rect.adjusted(5, 3, -5, -3), defaultAlignment(), text.replace(QChar('_'), ' '));
}
