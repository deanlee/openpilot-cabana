
#include "binary_delegate.h"

#include <QFontDatabase>
#include <QPainter>

#include "utils/util.h"
#include "binary_view.h"

MessageBytesDelegate::MessageBytesDelegate(QObject *parent) : QStyledItemDelegate(parent) {
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

void MessageBytesDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
  auto item = (const BinaryModel::Item *)index.internalPointer();
  BinaryView *bin_view = (BinaryView *)parent();
  painter->save();

  if (index.column() == 8) {
    if (item->valid) {
      painter->setFont(hex_font);
      painter->fillRect(option.rect, item->bg_color);
    }
  } else if (option.state & QStyle::State_Selected) {
    auto color = bin_view->resize_sig ? bin_view->resize_sig->color : option.palette.color(QPalette::Active, QPalette::Highlight);
    painter->fillRect(option.rect, color);
    painter->setPen(option.palette.color(QPalette::BrightText));
  } else if (!bin_view->selectionModel()->hasSelection() || !item->sigs.contains(bin_view->resize_sig)) {  // not resizing
    if (item->sigs.size() > 0) {
      for (auto &s : item->sigs) {
        if (s == bin_view->hovered_sig) {
          painter->fillRect(option.rect, s->color.darker(125));  // 4/5x brightness
        } else {
          drawSignalCell(painter, option, index, s);
        }
      }
    } else if (item->valid && item->bg_color.alpha() > 0) {
      painter->fillRect(option.rect, item->bg_color);
    }
    auto color_role = item->sigs.contains(bin_view->hovered_sig) ? QPalette::BrightText : QPalette::Text;
    painter->setPen(option.palette.color(bin_view->is_message_active ? QPalette::Normal : QPalette::Disabled, color_role));
  }

  if (item->sigs.size() > 1) {
    painter->fillRect(option.rect, QBrush(Qt::darkGray, Qt::Dense7Pattern));
  } else if (!item->valid) {
    painter->fillRect(option.rect, QBrush(Qt::darkGray, Qt::BDiagPattern));
  }
  if (item->valid) {
    utils::drawStaticText(painter, option.rect, index.column() == 8 ? hex_text_table[item->val] : bin_text_table[item->val]);
  }
  if ((item->is_msb || item->is_lsb) && item->sigs.size() == 1 && item->sigs[0]->size > 1) {
    painter->setFont(small_font);
    painter->drawText(option.rect.adjusted(8, 0, -8, -3), Qt::AlignRight | Qt::AlignBottom, item->is_msb ? "M" : "L");
  }
  painter->restore();
}

void MessageBytesDelegate::drawSignalCell(QPainter* painter, const QStyleOptionViewItem& option,
                                          const QModelIndex& index, const dbc::Signal* sig) const {
  auto item = static_cast<const BinaryModel::Item*>(index.internalPointer());
  const auto& b = item->borders;
  const int h_space = 3, v_space = 2;

  QRect rc = option.rect.adjusted(b.left * h_space, b.top * v_space,
                                  b.right * -h_space, b.bottom * -v_space);

  // Corner Correction using cached diagonals
  QRegion subtract;
  if (!b.top) {
    if (!b.left && b.top_left)
      subtract += QRect{rc.left(), rc.top(), h_space, v_space};
    else if (!b.right && b.top_right)
      subtract += QRect{rc.right() - (h_space - 1), rc.top(), h_space, v_space};
  }
  if (!b.bottom) {
    if (!b.left && b.bottom_left)
      subtract += QRect{rc.left(), rc.bottom() - (v_space - 1), h_space, v_space};
    else if (!b.right && b.bottom_right)
      subtract += QRect{rc.right() - (h_space - 1), rc.bottom() - (v_space - 1), h_space, v_space};
  }

  bool has_clip = !subtract.isEmpty();
  if (has_clip) {
    painter->setClipRegion(QRegion(rc).subtracted(subtract));
  }

  // Fill and Borders
  painter->fillRect(rc, option.palette.base());
  painter->fillRect(rc, item->bg_color);

  auto color = sig->color.darker(125);
  painter->setPen(QPen(color, 1));
  if (b.left) painter->drawLine(rc.topLeft(), rc.bottomLeft());
  if (b.right) painter->drawLine(rc.topRight(), rc.bottomRight());
  if (b.top) painter->drawLine(rc.topLeft(), rc.topRight());
  if (b.bottom) painter->drawLine(rc.bottomLeft(), rc.bottomRight());

  if (has_clip) {
    painter->setPen(QPen(color, 2, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    for (auto& r : subtract) {
      painter->drawRect(r);
    }
  }
}
