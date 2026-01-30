#include "message_delegate.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPainter>
#include <QStyle>

#include "utils/util.h"
#include "message_model.h"
#include "modules/inspector/history/history_model.h"

namespace {
// Internal helper to abstract data access from different models
struct MessageDataRef {
  uint8_t len = 0;
  const std::array<uint8_t, MAX_CAN_LEN>* bytes = nullptr;
  const std::array<uint32_t, MAX_CAN_LEN>* colors = nullptr;
};

MessageDataRef getDataRef(CallerType type, const QModelIndex& index) {
  if (type == CallerType::MessageList) {
    auto* item = static_cast<MessageModel::Item*>(index.internalPointer());
    return item->data ? MessageDataRef{item->data->size, &item->data->data, &item->data->colors} 
                      : MessageDataRef{0, nullptr, nullptr};
  } else {
    auto* msg = static_cast<MessageHistoryModel::Message*>(index.internalPointer());
    return msg ? MessageDataRef{msg->size, &msg->data, &msg->colors} 
               : MessageDataRef{0, nullptr, nullptr};
  }
}

const int GAP_WIDTH = 8; // Extra pixels added every 8 bytes
}  // namespace

MessageDelegate::MessageDelegate(QObject *parent, CallerType caller_type)
    : caller_type_(caller_type), QStyledItemDelegate(parent) {
  fixed_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);

  QFontMetrics fm(fixed_font);
  int hex_width = fm.horizontalAdvance("FF");
  int byte_gap = 4; // Gap between characters in a byte
  byte_size = QSize(hex_width + byte_gap, fm.height() + 2);

  h_margin = QApplication::style()->pixelMetric(QStyle::PM_FocusFrameHMargin) + 1;
  v_margin = QApplication::style()->pixelMetric(QStyle::PM_FocusFrameVMargin) + 1;

  updatePixmapCache(qApp->palette());
}

QSize MessageDelegate::sizeForBytes(int n) const {
  if (n <= 0) return {0, 0};

  // Account for 8-byte grouping gaps: (n-1)/8 gives number of gaps
  int num_gaps = (n - 1) / 8;
  int total_gap_width = num_gaps * GAP_WIDTH;

  int width = (n * byte_size.width()) + total_gap_width + (h_margin * 2);
  int height = byte_size.height() + (v_margin * 2);

  return {width, height};
}

QSize MessageDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
  if (!index.data(ColumnTypeRole::IsHexColumn).toBool()) {
    return QStyledItemDelegate::sizeHint(option, index);
  }

  MessageDataRef ref = getDataRef(caller_type_, index);
  return sizeForBytes(std::clamp((int)ref.len, 8, 64));
}

void MessageDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
  const bool is_selected = option.state & QStyle::State_Selected;

  // 1. Draw Background
  if (is_selected) {
    painter->fillRect(option.rect, option.palette.highlight());
  }

  const bool is_data_col = index.data(ColumnTypeRole::IsHexColumn).toBool();
  QVariant active_data = index.data(ColumnTypeRole::MsgActiveRole);
  const bool is_active = active_data.isValid() ? active_data.toBool() : true;

  // 2. Handle Non-Hex Columns (Standard text)
  if (!is_data_col) {
    painter->setPen(option.palette.color(is_active ? QPalette::Normal : QPalette::Disabled,
                                         is_selected ? QPalette::HighlightedText : QPalette::Text));
    QString text = index.data(Qt::DisplayRole).toString();
    if (!text.isEmpty()) {
      drawItemText(painter, option, index, text, is_selected);
    }
    return;
  }

  // 3. Handle Hex Column (CAN FD Data)
  MessageDataRef ref = getDataRef(caller_type_, index);
  if (!ref.bytes || ref.len == 0) return;

  const QRect content_rect = option.rect.adjusted(h_margin, v_margin, -h_margin, -v_margin);
  const int b_width = byte_size.width();
  const QPoint pt = content_rect.topLeft();

  // Cache boundaries to avoid repeated function calls in loop
  const int rect_left = option.rect.left();
  const int rect_right = option.rect.right();

  // Mathematical start: Jump directly to the first visible byte
  // We use a slightly conservative estimate to account for gaps
  int start_i = std::max(0, (rect_left - pt.x()) / (b_width + 1));

  const int state_idx = is_selected ? StateSelected : (!is_active ? StateDisabled : StateNormal);

  painter->save();
  painter->setClipRect(option.rect);  // Hard boundary to prevent overlap
  painter->setRenderHint(QPainter::Antialiasing, false);

  for (int i = start_i; i < ref.len; ++i) {
    // Calculate X: Byte width + 8px gap every 8 bytes
    const int x_pos = pt.x() + (i * b_width) + ((i / 8) * GAP_WIDTH);

    if (x_pos >= rect_right) break;
    if (x_pos + b_width < rect_left) continue;

    const QRect r(x_pos, pt.y(), b_width, byte_size.height());

    // Paint background highlight
    const uint32_t argb = (*ref.colors)[i];
    if (argb > 0x00FFFFFF) {  // Direct alpha check is faster than bit-shifting
      painter->fillRect(r, QColor::fromRgba(argb));
    }

    painter->drawPixmap(r.topLeft(), hex_pixmap_table[(*ref.bytes)[i]][state_idx]);
  }

  painter->restore();
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

  QColor colors[3] = {
      palette.color(QPalette::Normal, QPalette::Text),
      palette.color(QPalette::Normal, QPalette::HighlightedText),
      palette.color(QPalette::Disabled, QPalette::Text)
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
