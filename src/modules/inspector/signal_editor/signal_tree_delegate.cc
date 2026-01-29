#include "signal_tree_delegate.h"

#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainterPath>
#include <QSpinBox>
#include <QToolTip>

#include "core/commands/commands.h"
#include "value_table_editor.h"
#include "widgets/validators.h"

SignalTreeDelegate::SignalTreeDelegate(QObject* parent) : QStyledItemDelegate(parent) {
  name_validator = new NameValidator(this);
  node_validator = new QRegExpValidator(QRegExp("^\\w+(,\\w+)*$"), this);
  double_validator = new DoubleValidator(this);

  label_font.setPointSize(8);
  minmax_font.setPixelSize(10);
  value_font = qApp->font();
}

QSize SignalTreeDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
  // Use toolbar icon size + padding for row height; width determined by header
  int height = option.widget->style()->pixelMetric(QStyle::PM_ToolBarIconSize) + kPadding;
  return QSize(-1, height);
}

int SignalTreeDelegate::valueTextWidth(const QString& text) const {
  QFontMetrics fm(value_font);
  return fm.horizontalAdvance(text);
}

void SignalTreeDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
  auto item = static_cast<SignalTreeModel::Item*>(index.internalPointer());

  // Selection background
  if (option.state & QStyle::State_Selected) {
    painter->fillRect(option.rect, option.palette.highlight());
  } else if (option.state & QStyle::State_MouseOver || item->highlight) {
    painter->fillRect(option.rect, option.palette.color(QPalette::AlternateBase));
  }

  QRect rect = option.rect.adjusted(kPadding, 0, -kPadding, 0);

  if (index.column() == 0) {
    drawNameColumn(painter, rect, option, item, index);
  } else if (item->type == SignalTreeModel::Item::Sig) {
    drawDataColumn(painter, rect, option, item, index);
  } else {
    QStyledItemDelegate::paint(painter, option, index);
  }
}

void SignalTreeDelegate::drawNameColumn(QPainter* p, QRect r, const QStyleOptionViewItem& opt,
                                        SignalTreeModel::Item* item, const QModelIndex& idx) const {
  if (item->type == SignalTreeModel::Item::Sig) {
    // 1. Color Label / Row Number
    QRect iconRect(r.left(), r.top() + (r.height() - kBtnSize) / 2, kColorLabelW, kBtnSize);
    p->setPen(Qt::NoPen);
    p->setBrush(item->sig->color);
    p->drawRoundedRect(iconRect, 3, 3);  // Fixed: Direct drawing instead of roundedPath

    p->setFont(label_font);
    p->setPen(Qt::white);
    p->drawText(iconRect, Qt::AlignCenter, QString::number(item->row() + 1));

    r.setLeft(iconRect.right() + kPadding);

    // 2. Multiplexer Indicator (Badge)
    if (item->sig->type != dbc::Signal::Type::Normal) {
      QString m_text = (item->sig->type == dbc::Signal::Type::Multiplexor)
                           ? "M"
                           : QString("m%1").arg(item->sig->multiplex_value);

      int m_width = opt.fontMetrics.horizontalAdvance(m_text) + 8;
      QRect m_rect(r.left(), iconRect.top(), m_width, kBtnSize);

      p->setPen(Qt::NoPen);
      p->setBrush(opt.palette.dark());
      p->drawRoundedRect(m_rect, 2, 2);

      p->setPen(Qt::white);
      p->setFont(label_font);
      p->drawText(m_rect, Qt::AlignCenter, m_text);

      r.setLeft(m_rect.right() + kPadding);
    }
  }

  // 3. Signal Name / Property Label
  p->setFont(opt.font);
  p->setPen(opt.palette.color((opt.state & QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Text));

  QString text = idx.data(Qt::DisplayRole).toString();
  QString elidedText = opt.fontMetrics.elidedText(text, Qt::ElideRight, r.width());

  p->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, elidedText);
}

void SignalTreeDelegate::drawDataColumn(QPainter* p, QRect r, const QStyleOptionViewItem& opt, SignalTreeModel::Item* item, const QModelIndex& idx) const {
  const bool sel = opt.state & QStyle::State_Selected;
  const bool show_details = (sel || hoverIndex == idx) && !item->sparkline->isEmpty();
  const QColor text_c = opt.palette.color(sel ? QPalette::HighlightedText : QPalette::Text);

  // Sparkline
  int sparkW = 0;
  if (!item->sparkline->image.isNull()) {
    const auto& img = item->sparkline->image;
    sparkW = img.width() / img.devicePixelRatio();
    item->sparkline->setHighlight(sel);
    p->drawImage(r.left(), r.top() + (r.height() - img.height() / img.devicePixelRatio()) / 2, img);
  }

  // Details (Min/Max)
  int detailsW = 0;
  int anchorX = r.left() + sparkW;
  if (show_details) {
    p->setFont(minmax_font);
    QString maxS = utils::doubleToString(item->sparkline->max_val, item->sig->precision);
    QString minS = utils::doubleToString(item->sparkline->min_val, item->sig->precision);
    detailsW = std::max(p->fontMetrics().horizontalAdvance(maxS), p->fontMetrics().horizontalAdvance(minS)) + kPadding;

    p->setPen(sel ? text_c : opt.palette.mid().color());
    p->drawLine(anchorX, r.top() + 2, anchorX, r.bottom() - 2);

    p->setPen(sel ? text_c : opt.palette.placeholderText().color());
    QRect dRect(anchorX + kPadding, r.top(), detailsW, r.height());
    p->drawText(dRect, Qt::AlignTop, maxS);
    p->drawText(dRect, Qt::AlignBottom, minS);
  }

  // Value + Buttons
  QRect valR = r;
  valR.setLeft(anchorX + detailsW + kPadding);
  valR.setRight(r.right() - getButtonsWidth() - kPadding);

  p->setFont(value_font);
  p->setPen(text_c);
  p->drawText(valR, Qt::AlignRight | Qt::AlignVCenter, QFontMetrics(value_font).elidedText(item->sig_val, Qt::ElideRight, valR.width()));

  drawButtons(p, opt, item, idx);
}

QWidget* SignalTreeDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& idx) const {
  auto item = (SignalTreeModel::Item*)idx.internalPointer();
  using T = SignalTreeModel::Item::Type;

  if (item->type == T::ValueTable) {
    ValueTableEditor dlg(item->sig->value_table, parent);
    dlg.setWindowTitle(item->sig->name);
    if (dlg.exec()) const_cast<QAbstractItemModel*>(idx.model())->setData(idx, QVariant::fromValue(dlg.value_table));
    return nullptr;
  }

  if (item->type == T::SignalType) {
    auto* c = new QComboBox(parent);
    c->addItem(signalTypeToString(dbc::Signal::Type::Normal), (int)dbc::Signal::Type::Normal);
    auto* msg = GetDBC()->msg(((SignalTreeModel*)idx.model())->msg_id);
    if (!msg->multiplexor) c->addItem(signalTypeToString(dbc::Signal::Type::Multiplexor), (int)dbc::Signal::Type::Multiplexor);
    else if (item->sig->type != dbc::Signal::Type::Multiplexor) c->addItem(signalTypeToString(dbc::Signal::Type::Multiplexed), (int)dbc::Signal::Type::Multiplexed);
    return c;
  }

  if (item->type == T::Size) {
    auto* s = new QSpinBox(parent);
    s->setFrame(false);
    s->setRange(1, MAX_CAN_LEN);
    return s;
  }

  auto* e = new QLineEdit(parent);
  e->setFrame(false);
  if (item->type == T::Name) {
    e->setValidator(name_validator);
    auto* comp = new QCompleter(GetDBC()->signalNames(), e);
    comp->setCaseSensitivity(Qt::CaseInsensitive);
    e->setCompleter(comp);
  } else {
    e->setValidator(item->type == T::Node ? node_validator : double_validator);
  }
  return e;
}

void SignalTreeDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const {
  auto item = (SignalTreeModel::Item*)index.internalPointer();
  if (item->type == SignalTreeModel::Item::SignalType) {
    model->setData(index, ((QComboBox*)editor)->currentData().toInt());
    return;
  }
  QStyledItemDelegate::setModelData(editor, model, index);
}

int SignalTreeDelegate::buttonAt(const QPoint& pos, const QRect& rect) const {
  for (int i = 0; i < 2; ++i) {
    if (getButtonRect(rect, i).contains(pos)) return i;
  }
  return -1;
}

QRect SignalTreeDelegate::getButtonRect(const QRect& colRect, int btnIdx) const {
  // btnIdx 0: Remove (far right), btnIdx 1: Plot (left of remove)
  int x = colRect.right() - kPadding - kBtnSize - (btnIdx * (kBtnSize + kBtnSpacing));
  int y = colRect.top() + (colRect.height() - kBtnSize) / 2;
  return QRect(x, y, kBtnSize, kBtnSize);
}

void SignalTreeDelegate::drawButtons(QPainter* p, const QStyleOptionViewItem& opt, SignalTreeModel::Item* item, const QModelIndex& idx) const {
  bool chart_opened = idx.data(IsChartedRole).toBool();

  auto drawBtn = [&](int btnIdx, const QString& iconName, bool active) {
    QRect rect = getButtonRect(opt.rect, btnIdx);
    bool hovered = (hoverIndex == idx && hoverButton == btnIdx);
    bool selected = opt.state & QStyle::State_Selected;

    if (hovered || active) {
      // Background: Highlight if active, light overlay if hovered
      QColor bg = active ? opt.palette.color(QPalette::Highlight) : opt.palette.color(QPalette::Button);
      bg.setAlpha(active ? 255 : 100);
      p->setBrush(bg);
      QPen borderPen;
      if (active && selected) {
        // Bright white/light border for dark themes, or a distinct edge
        borderPen = QPen(opt.palette.color(QPalette::Base), 1.5);
      } else if (active) {
        borderPen = QPen(opt.palette.color(QPalette::Highlight).darker(150), 1);
      } else {
        borderPen = QPen(opt.palette.color(QPalette::Mid), 1);
      }
      p->setPen(borderPen);
      p->drawRoundedRect(rect.adjusted(1, 1, -1, -1), 4, 4);
    }

    // Icon rendering logic
    int iconPadding = 4;
    QSize iconSize = QSize(kBtnSize - (iconPadding * 2), kBtnSize - (iconPadding * 2));

    QColor icon_color;
    if (btnIdx == 0 && hovered) {
      icon_color = QColor(220, 53, 69);  // Soft Red
    } else {
      icon_color = (active || selected)
                       ? opt.palette.color(QPalette::HighlightedText)
                       : opt.palette.color(QPalette::Text);
    }
    QPixmap pix = utils::icon(iconName, iconSize, icon_color);
    p->drawPixmap(rect.left() + iconPadding, rect.top() + iconPadding, pix);
  };

  p->setRenderHint(QPainter::Antialiasing, true);
  // 0: Remove, 1: Plot
  drawBtn(0, "circle-minus", false);
  drawBtn(1, chart_opened ? "chart-area" : "chart-line", chart_opened);
  p->setRenderHint(QPainter::Antialiasing, false);
}

bool SignalTreeDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index) {
  auto item = static_cast<SignalTreeModel::Item*>(index.internalPointer());
  if (index.column() != 1 || !item || item->type != SignalTreeModel::Item::Sig) {
   return QStyledItemDelegate::helpEvent(event, view, option, index);
  }

  // Button Hit-Testing
  int btnIdx = buttonAt(event->pos(), option.rect);
  if (btnIdx != -1) {
    if (btnIdx == 1) {  // Plot Button
      bool opened = index.data(IsChartedRole).toBool();
      QToolTip::showText(event->globalPos(), opened ? tr("Close Plot") : tr("Show Plot\nSHIFT click to add to previous opened plot"), view);
    } else {  // Remove Button
      QToolTip::showText(event->globalPos(), tr("Remove Signal"), view);
    }
    return true;
  }

  // Value & Sparkline Area Hit-Testing
  int right_edge = option.rect.right() - getButtonsWidth();
  QRect value_rect = option.rect;
  value_rect.setLeft(right_edge - value_width);
  value_rect.setRight(right_edge);

  if (value_rect.contains(event->pos()) && !item->sig_val.isEmpty()) {
    QString tooltip = item->sig_val + "\n" +
                      tr("Session Min: %1\nSession Max: %2").arg(QString::number(item->sparkline->min_val, 'f', 3),
                                                QString::number(item->sparkline->max_val, 'f', 3));
    QToolTip::showText(event->globalPos(), tooltip, view);
    return true;
  }

  return QStyledItemDelegate::helpEvent(event, view, option, index);
}

bool SignalTreeDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                     const QStyleOptionViewItem& opt, const QModelIndex& idx) {
  auto item = static_cast<SignalTreeModel::Item*>(idx.internalPointer());

  if (!item || idx.column() != 1 || item->type != SignalTreeModel::Item::Sig) {
    return QStyledItemDelegate::editorEvent(event, model, opt, idx);
  }

  const auto type = event->type();
  const auto* mouseEvent = static_cast<QMouseEvent*>(event);
  const int btn = buttonAt(mouseEvent->pos(), opt.rect);

  if (type == QEvent::MouseMove) {
    if (hoverIndex != idx || hoverButton != btn) {
      QPersistentModelIndex oldIdx = hoverIndex;
      hoverIndex = idx;
      hoverButton = btn;

      if (auto* view = qobject_cast<QAbstractItemView*>(const_cast<QWidget*>(opt.widget))) {
        if (oldIdx.isValid()) view->update(oldIdx);
        if (hoverIndex.isValid()) view->update(hoverIndex);
      }
    }
    return false;
  }

  if (btn != -1) {
    if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick) {
      return true;  // Stop event propagation to prevent selection/expansion
    }

    if (type == QEvent::MouseButtonRelease) {
      if (btn == 1) {  // Plot Button
        bool isCharted = idx.data(IsChartedRole).toBool();
        emit plotRequested(item->sig, !isCharted, mouseEvent->modifiers() & Qt::ShiftModifier);
      } else if (btn == 0) {  // Remove Button
        clearHoverState();    // Reset before model potentially deletes 'item'
        emit removeRequested(item->sig);
      }
      return true;
    }
  }

  return QStyledItemDelegate::editorEvent(event, model, opt, idx);
}

int SignalTreeDelegate::nameColumnWidth(const dbc::Signal* sig) const {
  // Start with fixed decorative widths: Padding + Icon + Name Padding
  int width = kPadding + kColorLabelW + kPadding;

  if (sig->type != dbc::Signal::Type::Normal) {
    QString m_text = (sig->type == dbc::Signal::Type::Multiplexor) ? "M" : "m00";
    QFontMetrics badgeFm(label_font);
    width += badgeFm.horizontalAdvance(m_text) + 8 + kPadding;
  }

  QFontMetrics nameFm(QApplication::font());
  return width + nameFm.horizontalAdvance(sig->name) + (kPadding * 2);
}

void SignalTreeDelegate::clearHoverState() {
  if (hoverIndex.isValid()) {
    QPersistentModelIndex old = hoverIndex;
    hoverIndex = QPersistentModelIndex();
    hoverButton = -1;
    if (auto* view = qobject_cast<QAbstractItemView*>(parent())) {
      view->update(old);
    }
  } else {
    hoverIndex = QPersistentModelIndex();
    hoverButton = -1;
  }
}
