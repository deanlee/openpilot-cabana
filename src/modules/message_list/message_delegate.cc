#include "message_delegate.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPainter>
#include <QStyle>

#include "utils/util.h"

MessageDelegate::MessageDelegate(QObject *parent, bool multiple_lines)
    : font_metrics(QApplication::font()), multiple_lines(multiple_lines), QStyledItemDelegate(parent) {
  fixed_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  byte_size = QFontMetrics(fixed_font).size(Qt::TextSingleLine, "00 ") + QSize(0, 2);
  for (int i = 0; i < 256; ++i) {
    hex_text_table[i].setText(QStringLiteral("%1").arg(i, 2, 16, QLatin1Char('0')).toUpper());
    hex_text_table[i].prepare({}, fixed_font);
  }
  h_margin = QApplication::style()->pixelMetric(QStyle::PM_FocusFrameHMargin) + 1;
  v_margin = QApplication::style()->pixelMetric(QStyle::PM_FocusFrameVMargin) + 1;
}

QSize MessageDelegate::sizeForBytes(int n) const {
  int rows = multiple_lines ? std::max(1, n / 8) : 1;
  return {(n / rows) * byte_size.width() + h_margin * 2, rows * byte_size.height() + v_margin * 2};
}

QSize MessageDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
  auto data = index.data(BytesRole);
  return sizeForBytes(data.isValid() ? static_cast<std::vector<uint8_t> *>(data.value<void *>())->size() : 0);
}

void MessageDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
  auto data = index.data(BytesRole);
  if (!data.isValid()) {
    QStyledItemDelegate::paint(painter, option, index);
    return;
  }

  QStyleOptionViewItem opt = option;
  initStyleOption(&opt, index);
  QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter);

  painter->save();
  painter->setFont(fixed_font);

  const QRect item_rect = opt.rect.adjusted(h_margin, v_margin, -h_margin, -v_margin);
  const auto& bytes = *static_cast<std::vector<uint8_t>*>(data.value<void*>());
  const auto& colors = *static_cast<std::vector<QColor>*>(index.data(ColorsRole).value<void*>());

  QColor base_text_color = (opt.state & QStyle::State_Selected)
                               ? opt.palette.color(QPalette::HighlightedText)
                               : opt.palette.color(QPalette::Text);

  const QPoint pt = item_rect.topLeft();

  for (int i = 0; i < bytes.size(); ++i) {
    int row = !multiple_lines ? 0 : i / 8;
    int col = !multiple_lines ? i : i % 8;
    QRect r({pt.x() + col * byte_size.width(), pt.y() + row * byte_size.height()}, byte_size);

    // Byte-specific background (e.g., green/red change indicators)
    if (i < colors.size() && colors[i].alpha() > 0) {
      painter->fillRect(r, colors[i]);
      // Use standard text color for contrast against change-colors
      painter->setPen(opt.palette.color(QPalette::Text));
    } else {
      painter->setPen(base_text_color);
    }

    utils::drawStaticText(painter, r, hex_text_table[bytes[i]]);
  }

  painter->restore();
}