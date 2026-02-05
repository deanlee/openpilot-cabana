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
    auto* msg = static_cast<MessageHistoryModel::LogEntry*>(index.internalPointer());
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

  if (is_selected) {
    painter->fillRect(option.rect, option.palette.highlight());
  }

  QVariant active_data = index.data(ColumnTypeRole::MsgActiveRole);
  const bool is_active = active_data.isValid() ? active_data.toBool() : true;

  const bool is_data_col = index.data(ColumnTypeRole::IsHexColumn).toBool();
  if (!is_data_col) {
    drawItemText(painter, option, index, is_selected, is_active);
  } else {
    drawHexData(painter, option, index, is_selected, is_active);
  }
}

void MessageDelegate::drawItemText(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx, bool sel, bool active) const {
  QString text = idx.data(Qt::DisplayRole).toString();
  if (text.isEmpty()) return;

  p->setFont(opt.font);
  p->setPen(opt.palette.color(active ? QPalette::Normal : QPalette::Disabled,
                                    sel ? QPalette::HighlightedText : QPalette::Text));

  QRect textRect = opt.rect.adjusted(h_margin, 0, -h_margin, 0);
  const QFontMetrics &fm = opt.fontMetrics;
  const int y_baseline = textRect.top() + (textRect.height() - fm.height()) / 2 + fm.ascent();

  if (fm.horizontalAdvance(text) <= textRect.width()) {
    p->drawText(textRect.left(), y_baseline, text);
  } else {
    QString elided = fm.elidedText(text, Qt::ElideRight, textRect.width());
    p->drawText(textRect.left(), y_baseline, elided);
  }
}

void MessageDelegate::drawHexData(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx, bool sel, bool active) const {
  MessageDataRef ref = getDataRef(caller_type_, idx);
  if (!ref.bytes || ref.len == 0) return;

  const int x_start = opt.rect.left() + h_margin;
  const int y = opt.rect.top() + v_margin;
  const int state = sel ? StateSelected : (active ? StateNormal : StateDisabled);

  p->setRenderHint(QPainter::Antialiasing, false);

  for (int i = 0; i < ref.len; ++i) {
    const int x = x_start + (i * byte_size.width()) + ((i >> 3) * GAP_WIDTH);
    if (x + byte_size.width() > opt.rect.right()) break;

    const uint32_t argb = (*ref.colors)[i];
    if (argb > 0x00FFFFFF) {
      p->fillRect(x, y, byte_size.width(), byte_size.height(), QColor::fromRgba(argb));
    }
    p->drawPixmap(x, y, hex_pixmap_table[(*ref.bytes)[i]][state]);
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
