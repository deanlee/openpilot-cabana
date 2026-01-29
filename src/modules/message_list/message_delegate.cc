#include "message_delegate.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPainter>
#include <QStyle>

#include "utils/util.h"
#include "message_model.h"
#include "modules/inspector/history/history_model.h"

namespace {
struct MessageDataRef {
  uint8_t len = 0;
  const std::array<uint8_t, MAX_CAN_LEN>* bytes = nullptr;
  const std::array<uint32_t, MAX_CAN_LEN>* colors = nullptr;
};

MessageDataRef getDataRef(CallerType type, const QModelIndex& index) {
  if (type == CallerType::MessageList) {
    auto* item = static_cast<MessageModel::Item*>(index.internalPointer());
    return item->data ? MessageDataRef{item->data->size, &item->data->data, &item->data->colors} : MessageDataRef{0, nullptr, nullptr};
  } else {
    auto* msg = static_cast<MessageHistoryModel::Message*>(index.internalPointer());
    return msg ? MessageDataRef{msg->size, &msg->data, &msg->colors} : MessageDataRef{0, nullptr, nullptr};
  }
}

}  // namespace

MessageDelegate::MessageDelegate(QObject *parent, CallerType caller_type, bool multiple_lines)
    : caller_type_(caller_type), multiple_lines(multiple_lines), QStyledItemDelegate(parent) {
  fixed_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);

  QFontMetrics fm(fixed_font);
  byte_size = fm.size(Qt::TextSingleLine, "00 ") + QSize(0, 2);

  h_margin = QApplication::style()->pixelMetric(QStyle::PM_FocusFrameHMargin) + 1;
  v_margin = QApplication::style()->pixelMetric(QStyle::PM_FocusFrameVMargin) + 1;

  updatePixmapCache(qApp->palette());
}

QSize MessageDelegate::sizeForBytes(int n) const {
  if (n <= 0) return QSize(0, byte_size.height() + v_margin * 2);
  int rows = multiple_lines ? (n + 7) / 8 : 1;
  int cols = multiple_lines ? std::min(n, 8) : n;
  return {cols * byte_size.width() + h_margin * 2, rows * byte_size.height() + v_margin * 2};
}

QSize MessageDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
  if (!index.data(ColumnTypeRole::IsHexColumn).toBool()) {
    return QStyledItemDelegate::sizeHint(option, index);
  }
  MessageDataRef ref = getDataRef(caller_type_, index);
  return sizeForBytes(ref.len);
}

void MessageDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
  const bool is_selected = option.state & QStyle::State_Selected;
  if (is_selected) {
    painter->fillRect(option.rect, option.palette.highlight());
  }

  QVariant data = index.data(ColumnTypeRole::MsgActiveRole);
  bool is_active = data.isValid() ? data.toBool() : true;
  painter->setPen(option.palette.color(is_active ? QPalette::Normal : QPalette::Disabled,
                                       is_selected ? QPalette::HighlightedText : QPalette::Text));

  bool is_data_col = index.data(ColumnTypeRole::IsHexColumn).toBool();
  if (!is_data_col) {
    QString text = index.data(Qt::DisplayRole).toString();
    if (!text.isEmpty()) {
      drawItemText(painter, option, index, text, is_selected);
    }
    return;
  }

  MessageDataRef ref = getDataRef(caller_type_, index);
  if (!ref.bytes || ref.bytes->empty()) return;

  const QRect item_rect = option.rect.adjusted(h_margin, v_margin, -h_margin, -v_margin);
  const auto& bytes = *ref.bytes;
  const auto& colors = *ref.colors;
  const int b_width = byte_size.width();
  const int b_height = byte_size.height();
  const QPoint pt = item_rect.topLeft();

  int state_idx = StateNormal; // Normal
  if (is_selected) state_idx = StateSelected;
  else if (!is_active) state_idx = StateDisabled;

  painter->setRenderHint(QPainter::Antialiasing, false);
  for (int i = 0; i < ref.len; ++i) {
    int row = !multiple_lines ? 0 : i / 8;
    int col = !multiple_lines ? i : i % 8;
    QRect r(pt.x() + (col * b_width), pt.y() + (row * b_height), b_width, b_height);
    uint32_t argb = colors[i];
    if ((argb >> 24) > 1) {
      painter->fillRect(r, QColor::fromRgba(argb));
    }
    painter->drawPixmap(r.topLeft(), hex_pixmap_table[bytes[i]][state_idx]);
  }
}

void MessageDelegate::drawItemText(QPainter* painter, const QStyleOptionViewItem& option,
                                   const QModelIndex& index, const QString& text, bool is_selected) const {
  painter->setFont(option.font);

  QRect textRect = option.rect.adjusted(h_margin, 0, -h_margin, 0);
  const QFontMetrics &fm = option.fontMetrics;
  const int y_baseline = textRect.top() + (textRect.height() - fm.height()) / 2 + fm.ascent();

  if (fm.horizontalAdvance(text) <= textRect.width()) {
    painter->drawText(textRect.left(), y_baseline, text);
  } else {
    QString elided = fm.elidedText(text, Qt::ElideRight, textRect.width());
    painter->drawText(textRect.left(), y_baseline, elided);
  }
}

void MessageDelegate::updatePixmapCache(const QPalette& palette) const {
  if (!hex_pixmap_table[0][0].isNull() && cached_palette == palette) return;

  cached_palette = palette;
  qreal dpr = qApp->devicePixelRatio();

  // Define colors for the 3 states
  QColor colors[3] = {
      palette.color(QPalette::Normal, QPalette::Text),             // 0: Normal
      palette.color(QPalette::Normal, QPalette::HighlightedText),  // 1: Selected
      palette.color(QPalette::Disabled, QPalette::Text)            // 2: Disabled
  };

  for (int i = 0; i < 256; ++i) {
    QString hex = QString("%1").arg(i, 2, 16, QLatin1Char('0')).toUpper();
    for (int s = 0; s < 3; ++s) {
      QPixmap pix(byte_size * dpr);
      pix.setDevicePixelRatio(dpr);
      pix.fill(Qt::transparent);

      QPainter p(&pix);
      p.setFont(fixed_font);
      p.setPen(colors[s]);
      p.drawText(pix.rect(), Qt::AlignCenter, hex);
      p.end();

      hex_pixmap_table[i][s] = pix;
    }
  }
}
