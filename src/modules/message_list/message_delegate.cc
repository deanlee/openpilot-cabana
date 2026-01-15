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
  const std::vector<uint8_t>* bytes = nullptr;
  const std::vector<QColor>* colors = nullptr;
};

MessageDataRef getDataRef(CallerType type, const QModelIndex& index) {
  if (type == CallerType::MessageList) {
    auto* item = static_cast<MessageModel::Item*>(index.internalPointer());
    return item->data ? MessageDataRef{&item->data->dat, &item->data->colors} : MessageDataRef{nullptr, nullptr};
  } else {
    auto* msg = static_cast<MessageHistoryModel::Message*>(index.internalPointer());
    return msg ? MessageDataRef{&msg->data, &msg->colors} : MessageDataRef{nullptr, nullptr};
  }
}

}  // namespace

MessageDelegate::MessageDelegate(QObject *parent, CallerType caller_type, bool multiple_lines)
    : caller_type_(caller_type), multiple_lines(multiple_lines), QStyledItemDelegate(parent) {
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
  if (n <= 0) return QSize(0, byte_size.height() + v_margin * 2);
  int rows = multiple_lines ? (n + 7) / 8 : 1;
  int cols = multiple_lines ? std::min(n, 8) : n;
  return {cols * byte_size.width() + h_margin * 2, rows * byte_size.height() + v_margin * 2};
}

QSize MessageDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
  MessageDataRef ref = getDataRef(caller_type_, index);
  return sizeForBytes(ref.bytes ? ref.bytes->size() : 0);
}

void MessageDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
  MessageDataRef ref = getDataRef(caller_type_, index);
  if (!ref.bytes || ref.bytes->empty()) {
    QStyledItemDelegate::paint(painter, option, index);
    return;
  }

  if (option.state & QStyle::State_Selected) {
    painter->fillRect(option.rect, option.palette.highlight());
  }

  painter->setFont(fixed_font);

  const QRect item_rect = option.rect.adjusted(h_margin, v_margin, -h_margin, -v_margin);
  const auto& bytes = *ref.bytes;
  const auto& colors = *ref.colors;
  QColor text_color = (option.state & QStyle::State_Selected)
                          ? option.palette.color(QPalette::HighlightedText)
                          : option.palette.color(QPalette::Text);
  painter->setPen(text_color);
  const QPoint pt = item_rect.topLeft();

  for (int i = 0; i < bytes.size(); ++i) {
    int row = !multiple_lines ? 0 : i / 8;
    int col = !multiple_lines ? i : i % 8;
    QRect r({pt.x() + col * byte_size.width(), pt.y() + row * byte_size.height()}, byte_size);

    // Byte-specific background (e.g., green/red change indicators)
    if (i < colors.size() && colors[i].alpha() > 1) {
      painter->fillRect(r, colors[i]);
    }
    utils::drawStaticText(painter, r, hex_text_table[bytes[i]]);
  }
}
