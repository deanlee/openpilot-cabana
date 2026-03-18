#include "binary_delegate.h"

#include <QFontDatabase>
#include <QPainter>

#include "binary_view.h"
#include "utils/util.h"

MessageBytesDelegate::MessageBytesDelegate(QObject* parent) : QStyledItemDelegate(parent) {
  label_font.setPixelSize(8);
  hex_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  hex_font.setBold(true);

  bit_text_cache[0].setText("0");
  bit_text_cache[1].setText("1");
  for (int i = 0; i < 256; ++i) {
    hex_text_cache[i].setText(QString::asprintf("%02X", i));
    hex_text_cache[i].prepare({}, hex_font);
  }
}

void MessageBytesDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
  const auto* item = static_cast<const BinaryModel*>(index.model())->getItem(index);
  const auto* binary_view = static_cast<const BinaryView*>(parent());
  const bool is_hex = (index.column() == 8);
  const bool is_selected = option.state & QStyle::State_Selected;
  const bool has_valid_value = item->value != BinaryModel::INVALID_BIT;

  // 1. Background
  if (is_hex) {
    if (has_valid_value) {
      painter->setFont(hex_font);
      painter->fillRect(option.rect, item->bg_color);
    }
  } else if (is_selected) {
    auto color = binary_view->resizing_signal ? binary_view->resizing_signal->color
                                      : option.palette.color(QPalette::Active, QPalette::Highlight);
    painter->fillRect(option.rect, color);
  } else if (!item->signal_list.empty()) {
    for (const auto* s : item->signal_list) {
      if (s == binary_view->hovered_signal) {
        painter->fillRect(option.rect, s->color.darker(125));
      } else {
        drawSignalCell(painter, option, item, s);
      }
    }
  } else if (has_valid_value && item->bg_color.alpha() > 0) {
    painter->fillRect(option.rect, item->bg_color);
  }

  // 2. Overlap indicator
  if (item->signal_list.size() > 1) {
    painter->fillRect(option.rect, QBrush(Qt::darkGray, Qt::Dense7Pattern));
  }

  // 3. Text
  const bool use_bright_text = is_selected || item->signal_list.contains(binary_view->hovered_signal);
  painter->setPen(option.palette.color(use_bright_text ? QPalette::BrightText : QPalette::Text));
  if (has_valid_value) {
    painter->setFont(is_hex ? hex_font : option.font);
    utils::drawStaticText(painter, option.rect, is_hex ? hex_text_cache[item->value] : bit_text_cache[item->value]);
  } else {
    painter->setFont(option.font);
    painter->drawText(option.rect, Qt::AlignCenter, QStringLiteral("-"));
  }

  // 4. MSB/LSB label
  if ((item->is_msb || item->is_lsb) && item->signal_list.size() == 1 && item->signal_list[0]->size > 1) {
    painter->setFont(label_font);
    painter->drawText(option.rect.adjusted(8, 0, -8, -3), Qt::AlignRight | Qt::AlignBottom, item->is_msb ? "M" : "L");
  }
}

void MessageBytesDelegate::drawSignalCell(QPainter* painter, const QStyleOptionViewItem& option,
                                          const BinaryModel::Item* item, const dbc::Signal* sig) const {
  const auto& borders = item->borders;
  constexpr int h_pad = 3, v_pad = 2;

  const QRect cell_rect = option.rect.adjusted(borders.left * h_pad, borders.top * v_pad, borders.right * -h_pad, borders.bottom * -v_pad);

  // Logic: Fill center, then fill top/bottom rows with calculated widths to "carve" corners
  if (borders.top_left || borders.top_right || borders.bottom_left || borders.bottom_right) {
    // Fill vertical center (area between top and bottom padding rows)
    painter->fillRect(cell_rect.left(), cell_rect.top() + v_pad, cell_rect.width(), cell_rect.height() - (2 * v_pad), item->bg_color);

    // Top padding row
    int top_x = cell_rect.left() + ((!borders.top && !borders.left && borders.top_left) ? h_pad : 0);
    int top_width = cell_rect.width() - (top_x - cell_rect.left()) - ((!borders.top && !borders.right && borders.top_right) ? h_pad : 0);
    painter->fillRect(top_x, cell_rect.top(), top_width, v_pad, item->bg_color);

    // Bottom padding row
    int bottom_x = cell_rect.left() + ((!borders.bottom && !borders.left && borders.bottom_left) ? h_pad : 0);
    int bottom_width = cell_rect.width() - (bottom_x - cell_rect.left()) - ((!borders.bottom && !borders.right && borders.bottom_right) ? h_pad : 0);
    painter->fillRect(bottom_x, cell_rect.bottom() - v_pad + 1, bottom_width, v_pad, item->bg_color);
  } else {
    painter->fillRect(cell_rect, item->bg_color);
  }

  // Batched Border Drawing
  QPen borderPen(sig->color.darker(125), 0);
  painter->setPen(borderPen);

  QLine lines[8];  // Max 4 borders + 4 corner pieces
  int line_count = 0;

  if (borders.left) lines[line_count++] = QLine(cell_rect.topLeft(), cell_rect.bottomLeft());
  if (borders.right) lines[line_count++] = QLine(cell_rect.topRight(), cell_rect.bottomRight());
  if (borders.top) lines[line_count++] = QLine(cell_rect.topLeft(), cell_rect.topRight());
  if (borders.bottom) lines[line_count++] = QLine(cell_rect.bottomLeft(), cell_rect.bottomRight());

  // L-Shaped Corner Borders (only if no main border exists)
  if (!borders.top) {
    if (!borders.left && borders.top_left) {
      lines[line_count++] = QLine(cell_rect.left(), cell_rect.top() + v_pad, cell_rect.left() + h_pad, cell_rect.top() + v_pad);
      lines[line_count++] = QLine(cell_rect.left() + h_pad, cell_rect.top(), cell_rect.left() + h_pad, cell_rect.top() + v_pad);
    }
    if (!borders.right && borders.top_right) {
      lines[line_count++] = QLine(cell_rect.right() - h_pad, cell_rect.top(), cell_rect.right() - h_pad, cell_rect.top() + v_pad);
      lines[line_count++] = QLine(cell_rect.right() - h_pad, cell_rect.top() + v_pad, cell_rect.right(), cell_rect.top() + v_pad);
    }
  }
  if (!borders.bottom) {
    if (!borders.left && borders.bottom_left) {
      lines[line_count++] = QLine(cell_rect.left(), cell_rect.bottom() - v_pad, cell_rect.left() + h_pad, cell_rect.bottom() - v_pad);
      lines[line_count++] = QLine(cell_rect.left() + h_pad, cell_rect.bottom() - v_pad, cell_rect.left() + h_pad, cell_rect.bottom());
    }
    if (!borders.right && borders.bottom_right) {
      lines[line_count++] = QLine(cell_rect.right() - h_pad, cell_rect.bottom() - v_pad, cell_rect.right(), cell_rect.bottom() - v_pad);
      lines[line_count++] = QLine(cell_rect.right() - h_pad, cell_rect.bottom() - v_pad, cell_rect.right() - h_pad, cell_rect.bottom());
    }
  }

  if (line_count > 0) {
    painter->drawLines(lines, line_count);
  }
}
