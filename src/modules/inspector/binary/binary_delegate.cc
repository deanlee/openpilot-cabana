#include "binary_delegate.h"

#include <QFontDatabase>
#include <QPainter>

#include "binary_view.h"
#include "utils/util.h"

MessageBytesDelegate::MessageBytesDelegate(QObject* parent) : QStyledItemDelegate(parent) {
  small_font.setPixelSize(8);
  hex_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  hex_font.setBold(true);

  bin_text_table[0].setText("0");
  bin_text_table[1].setText("1");
  for (int i = 0; i < 256; ++i) {
    hex_text_table[i].setText(QStringLiteral("%1").arg(i, 2, 16, QLatin1Char('0')).toUpper());
    hex_text_table[i].prepare({}, hex_font);
  }
}

void MessageBytesDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
  auto item = static_cast<const BinaryModel::Item*>(index.internalPointer());
  auto* bin_view = static_cast<BinaryView*>(parent());

  const bool is_hex = (index.column() == 8);
  const bool is_selected = option.state & QStyle::State_Selected;
  const bool item_has_hovered = item->sigs.contains(bin_view->hovered_sig);
  const bool has_valid_val = item->val != BinaryModel::INVALID_BIT;

  if (is_hex) {
    if (has_valid_val) {
      painter->setFont(hex_font);
      painter->fillRect(option.rect, item->bg_color);
    }
  } else if (is_selected) {
    auto color = bin_view->resize_sig ? bin_view->resize_sig->color
                                      : option.palette.color(QPalette::Active, QPalette::Highlight);
    painter->fillRect(option.rect, color);
  } else if (!bin_view->selectionModel()->hasSelection() ||
             !item->sigs.contains(bin_view->resize_sig)) {  // not resizing
    if (!item->sigs.empty()) {
      for (auto s : item->sigs) {
        if (s == bin_view->hovered_sig) {
          painter->fillRect(option.rect, s->color.darker(125));
        } else {
          drawSignalCell(painter, option, index, s);
        }
      }
    } else if (has_valid_val && item->bg_color.alpha() > 0) {
      painter->fillRect(option.rect, item->bg_color);
    }
  }

  if (item->sigs.size() > 1) {
    painter->fillRect(option.rect, QBrush(Qt::darkGray, Qt::Dense7Pattern));
  } else if (!has_valid_val) {
    painter->fillRect(option.rect, QBrush(Qt::darkGray, Qt::BDiagPattern));
  }

  if (has_valid_val) {
    auto color_role = (item_has_hovered || is_selected) ? QPalette::BrightText : QPalette::Text;
    auto group = bin_view->is_message_active ? QPalette::Normal : QPalette::Disabled;
    painter->setPen(option.palette.color(group, color_role));
    painter->setFont(is_hex ? hex_font : option.font);
    utils::drawStaticText(painter, option.rect, is_hex ? hex_text_table[item->val] : bin_text_table[item->val]);
  }

  if ((item->is_msb || item->is_lsb) && item->sigs.size() == 1 && item->sigs[0]->size > 1) {
    painter->setFont(small_font);
    painter->drawText(option.rect.adjusted(8, 0, -8, -3), Qt::AlignRight | Qt::AlignBottom, item->is_msb ? "M" : "L");
  }
}

void MessageBytesDelegate::drawSignalCell(QPainter* painter, const QStyleOptionViewItem& option,
                                          const QModelIndex& index, const dbc::Signal* sig) const {
  const auto* item = static_cast<const BinaryModel::Item*>(index.internalPointer());
  const auto& b = item->borders;
  constexpr int h_space = 3, v_space = 2;

  const QRect rc = option.rect.adjusted(b.left * h_space, b.top * v_space, b.right * -h_space, b.bottom * -v_space);

  // Logic: Fill center, then fill top/bottom rows with calculated widths to "carve" corners
  if (b.top_left || b.top_right || b.bottom_left || b.bottom_right) {
    // Fill vertical center (area between top and bottom padding rows)
    painter->fillRect(rc.left(), rc.top() + v_space, rc.width(), rc.height() - (2 * v_space), item->bg_color);

    // Top padding row
    int tx = rc.left() + ((!b.top && !b.left && b.top_left) ? h_space : 0);
    int tw = rc.width() - (tx - rc.left()) - ((!b.top && !b.right && b.top_right) ? h_space : 0);
    painter->fillRect(tx, rc.top(), tw, v_space, item->bg_color);

    // Bottom padding row
    int bx = rc.left() + ((!b.bottom && !b.left && b.bottom_left) ? h_space : 0);
    int bw = rc.width() - (bx - rc.left()) - ((!b.bottom && !b.right && b.bottom_right) ? h_space : 0);
    painter->fillRect(bx, rc.bottom() - v_space + 1, bw, v_space, item->bg_color);
  } else {
    painter->fillRect(rc, item->bg_color);
  }

  // Batched Border Drawing
  QPen borderPen(sig->color.darker(125), 0);
  painter->setPen(borderPen);

  QLine lines[8];  // Max 4 borders + 4 corner pieces
  int l_idx = 0;

  if (b.left) lines[l_idx++] = QLine(rc.topLeft(), rc.bottomLeft());
  if (b.right) lines[l_idx++] = QLine(rc.topRight(), rc.bottomRight());
  if (b.top) lines[l_idx++] = QLine(rc.topLeft(), rc.topRight());
  if (b.bottom) lines[l_idx++] = QLine(rc.bottomLeft(), rc.bottomRight());

  // L-Shaped Corner Borders (only if no main border exists)
  if (!b.top) {
    if (!b.left && b.top_left) {
      lines[l_idx++] = QLine(rc.left(), rc.top() + v_space, rc.left() + h_space, rc.top() + v_space);
      lines[l_idx++] = QLine(rc.left() + h_space, rc.top(), rc.left() + h_space, rc.top() + v_space);
    }
    if (!b.right && b.top_right) {
      lines[l_idx++] = QLine(rc.right() - h_space, rc.top(), rc.right() - h_space, rc.top() + v_space);
      lines[l_idx++] = QLine(rc.right() - h_space, rc.top() + v_space, rc.right(), rc.top() + v_space);
    }
  }
  if (!b.bottom) {
    if (!b.left && b.bottom_left) {
      lines[l_idx++] = QLine(rc.left(), rc.bottom() - v_space, rc.left() + h_space, rc.bottom() - v_space);
      lines[l_idx++] = QLine(rc.left() + h_space, rc.bottom() - v_space, rc.left() + h_space, rc.bottom());
    }
    if (!b.right && b.bottom_right) {
      lines[l_idx++] = QLine(rc.right() - h_space, rc.bottom() - v_space, rc.right(), rc.bottom() - v_space);
      lines[l_idx++] = QLine(rc.right() - h_space, rc.bottom() - v_space, rc.right() - h_space, rc.bottom());
    }
  }

  if (l_idx > 0) {
    painter->drawLines(lines, l_idx);
  }
}
