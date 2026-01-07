
#include "message_bytes.h"

#include <QFontDatabase>
#include <QPainter>

#include "utils/util.h"
#include "widgets/binary_view.h"

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

bool MessageBytesDelegate::hasSignal(const QModelIndex &index, int dx, int dy, const dbc::Signal *sig) const {
  if (!index.isValid()) return false;
  auto model = (const MessageBytesModel*)(index.model());
  int idx = (index.row() + dy) * model->columnCount() + index.column() + dx;
  return (idx >=0 && idx < model->items.size()) ? model->items[idx].sigs.contains(sig) : false;
}

void MessageBytesDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
  auto item = (const MessageBytesModel::Item *)index.internalPointer();
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
  if (item->is_msb || item->is_lsb) {
    painter->setFont(small_font);
    painter->drawText(option.rect.adjusted(8, 0, -8, -3), Qt::AlignRight | Qt::AlignBottom, item->is_msb ? "M" : "L");
  }
  painter->restore();
}

// Draw border on edge of signal
void MessageBytesDelegate::drawSignalCell(QPainter *painter, const QStyleOptionViewItem &option,
                                        const QModelIndex &index, const dbc::Signal *sig) const {
  bool draw_left = !hasSignal(index, -1, 0, sig);
  bool draw_top = !hasSignal(index, 0, -1, sig);
  bool draw_right = !hasSignal(index, 1, 0, sig);
  bool draw_bottom = !hasSignal(index, 0, 1, sig);

  const int spacing = 2;
  QRect rc = option.rect.adjusted(draw_left * 3, draw_top * spacing, draw_right * -3, draw_bottom * -spacing);
  QRegion subtract;
  if (!draw_top) {
    if (!draw_left && !hasSignal(index, -1, -1, sig)) {
      subtract += QRect{rc.left(), rc.top(), 3, spacing};
    } else if (!draw_right && !hasSignal(index, 1, -1, sig)) {
      subtract += QRect{rc.right() - 2, rc.top(), 3, spacing};
    }
  }
  if (!draw_bottom) {
    if (!draw_left && !hasSignal(index, -1, 1, sig)) {
      subtract += QRect{rc.left(), rc.bottom() - (spacing - 1), 3, spacing};
    } else if (!draw_right && !hasSignal(index, 1, 1, sig)) {
      subtract += QRect{rc.right() - 2, rc.bottom() - (spacing - 1), 3, spacing};
    }
  }
  painter->setClipRegion(QRegion(rc).subtracted(subtract));

  auto item = (const MessageBytesModel::Item *)index.internalPointer();
  QColor color = sig->color;
  color.setAlpha(item->bg_color.alpha());
  // Mixing the signal color with the Base background color to fade it
  painter->fillRect(rc, option.palette.color(QPalette::Base));
  painter->fillRect(rc, color);

  // Draw edges
  color = sig->color.darker(125);
  painter->setPen(QPen(color, 1));
  if (draw_left) painter->drawLine(rc.topLeft(), rc.bottomLeft());
  if (draw_right) painter->drawLine(rc.topRight(), rc.bottomRight());
  if (draw_bottom) painter->drawLine(rc.bottomLeft(), rc.bottomRight());
  if (draw_top) painter->drawLine(rc.topLeft(), rc.topRight());

  if (!subtract.isEmpty()) {
    // fill gaps inside corners.
    painter->setPen(QPen(color, 2, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    for (auto &r : subtract) {
      painter->drawRect(r);
    }
  }
}
