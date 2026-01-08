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

void MessageDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
  if (option.state & QStyle::State_Selected) {
    painter->fillRect(option.rect, option.palette.brush(QPalette::Normal, QPalette::Highlight));
  }

  QRect item_rect = option.rect.adjusted(h_margin, v_margin, -h_margin, -v_margin);
  QColor highlighted_color = option.palette.color(QPalette::HighlightedText);
  auto text_color = index.data(Qt::ForegroundRole).value<QColor>();
  bool inactive = text_color.isValid();
  if (!inactive) {
    text_color = option.palette.color(QPalette::Text);
  }
  auto data = index.data(BytesRole);
  if (!data.isValid()) {
    painter->setFont(option.font);
    painter->setPen(option.state & QStyle::State_Selected ? highlighted_color : text_color);
    QString text = font_metrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, item_rect.width());
    painter->drawText(item_rect, Qt::AlignLeft | Qt::AlignVCenter, text);
    return;
  }

  // Paint hex column
  const auto &bytes = *static_cast<std::vector<uint8_t> *>(data.value<void *>());
  const auto &colors = *static_cast<std::vector<QColor> *>(index.data(ColorsRole).value<void *>());

  painter->setFont(fixed_font);
  const QPen text_pen(option.state & QStyle::State_Selected ? highlighted_color : text_color);
  const QPoint pt = item_rect.topLeft();
  for (int i = 0; i < bytes.size(); ++i) {
    int row = !multiple_lines ? 0 : i / 8;
    int column = !multiple_lines ? i : i % 8;
    QRect r({pt.x() + column * byte_size.width(), pt.y() + row * byte_size.height()}, byte_size);

    if (!inactive && i < colors.size() && colors[i].alpha() > 0) {
      if (option.state & QStyle::State_Selected) {
        painter->setPen(option.palette.color(QPalette::Text));
        painter->fillRect(r, option.palette.color(QPalette::Window));
      }
      painter->fillRect(r, colors[i]);
    } else {
      painter->setPen(text_pen);
    }
    utils::drawStaticText(painter, r, hex_text_table[bytes[i]]);
  }
}
