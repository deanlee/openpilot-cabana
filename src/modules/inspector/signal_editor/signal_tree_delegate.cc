#include "signal_tree_delegate.h"

#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSpinBox>
#include <QToolTip>

#include "core/commands/commands.h"
#include "value_table_editor.h"
#include "widgets/validators.h"

SignalTreeDelegate::SignalTreeDelegate(QObject* parent) : QStyledItemDelegate(parent) {
  nameValidator_ = new NameValidator(this);
  nodeValidator_ = new QRegularExpressionValidator(QRegularExpression("^\\w+(,\\w+)*$"), this);
  doubleValidator_ = new DoubleValidator(this);
  signalTextColor_ = utils::isDarkTheme() ? QColor(20, 20, 20) : QColor(255, 255, 255);

  labelFont_.setPointSize(8);
  minmaxFont_.setPixelSize(10);
  valueFont_ = qApp->font();
}

QSize SignalTreeDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
  int height = option.widget->style()->pixelMetric(QStyle::PM_ToolBarIconSize) + kPadding;
  return QSize(-1, height);
}

void SignalTreeDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                               const QModelIndex& index) const {
  auto* item = static_cast<TreeItem*>(index.internalPointer());

  // Draw selection/hover background
  bool isSelected = option.state & QStyle::State_Selected;
  bool isHighlighted = false;
  if (auto* sigItem = dynamic_cast<SignalItem*>(item)) {
    isHighlighted = sigItem->highlight;
  }

  if (isSelected) {
    painter->fillRect(option.rect, option.palette.highlight());
  } else if ((option.state & QStyle::State_MouseOver) || isHighlighted) {
    painter->fillRect(option.rect, option.palette.color(QPalette::AlternateBase));
  }

  // Dispatch to specialized paint methods
  if (auto* sigItem = dynamic_cast<SignalItem*>(item)) {
    paintSignalRow(painter, option, sigItem, index);
  } else if (auto* propItem = dynamic_cast<PropertyItem*>(item)) {
    paintPropertyRow(painter, option, propItem, index);
  }
}

void SignalTreeDelegate::paintSignalRow(QPainter* p, const QStyleOptionViewItem& opt, SignalItem* item,
                                        const QModelIndex& idx) const {
  QRect rect = opt.rect.adjusted(kPadding, 0, -kPadding, 0);

  if (idx.column() == 0) {
    paintSignalNameColumn(p, rect, opt, item);
  } else {
    paintSignalValueColumn(p, rect, opt, item, idx);
  }
}

void SignalTreeDelegate::paintSignalNameColumn(QPainter* p, QRect rect, const QStyleOptionViewItem& opt,
                                               SignalItem* item) const {
  // 1. Draw color label with row number
  QRect iconRect(rect.left(), rect.top() + (rect.height() - kBtnSize) / 2, kColorLabelW, kBtnSize);
  p->setPen(Qt::NoPen);
  p->setBrush(item->sig->color);
  p->drawRoundedRect(iconRect, 3, 3);

  p->setFont(labelFont_);
  p->setPen(signalTextColor_);
  p->drawText(iconRect, Qt::AlignCenter, QString::number(item->row() + 1));

  rect.setLeft(iconRect.right() + kPadding);

  // 2. Draw multiplexer badge if needed
  if (item->sig->type != dbc::Signal::Type::Normal) {
    QString badgeText =
        (item->sig->type == dbc::Signal::Type::Multiplexor) ? "M" : QString("m%1").arg(item->sig->multiplex_value);

    int badgeWidth = opt.fontMetrics.horizontalAdvance(badgeText) + 8;
    QRect badgeRect(rect.left(), iconRect.top(), badgeWidth, kBtnSize);

    p->setPen(Qt::NoPen);
    p->setBrush(opt.palette.dark());
    p->drawRoundedRect(badgeRect, 2, 2);

    p->setPen(Qt::white);
    p->setFont(labelFont_);
    p->drawText(badgeRect, Qt::AlignCenter, badgeText);

    rect.setLeft(badgeRect.right() + kPadding);
  }

  // 3. Draw signal name
  p->setFont(opt.font);
  QPalette::ColorRole textRole = (opt.state & QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Text;
  p->setPen(opt.palette.color(textRole));

  QString elidedText = opt.fontMetrics.elidedText(item->sig->name, Qt::ElideRight, rect.width());
  p->drawText(rect, Qt::AlignVCenter | Qt::AlignLeft, elidedText);
}

void SignalTreeDelegate::paintSignalValueColumn(QPainter* p, QRect rect, const QStyleOptionViewItem& opt,
                                                SignalItem* item, const QModelIndex& idx) const {
  const bool selected = opt.state & QStyle::State_Selected;
  const bool showDetails = (hoverIndex_ == idx) && !item->sparkline->isEmpty();
  const QColor textColor = opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);

  // Draw sparkline
  int sparkWidth = 0;
  if (!item->sparkline->image().isNull()) {
    const QImage& img = item->sparkline->image();
    const qreal dpr = img.devicePixelRatio();
    sparkWidth = img.width() / dpr;
    item->sparkline->setHighlight(selected);
    p->drawImage(rect.left(), rect.top() + (rect.height() - img.height() / dpr) / 2, img);
  }

  // Draw min/max details on hover
  int detailsWidth = 0;
  int anchorX = rect.left() + sparkWidth;

  if (showDetails) {
    p->setFont(minmaxFont_);
    QString maxStr = utils::doubleToString(item->sparkline->maxVal(), item->sig->precision);
    QString minStr = utils::doubleToString(item->sparkline->minVal(), item->sig->precision);
    detailsWidth = std::max(p->fontMetrics().horizontalAdvance(maxStr), p->fontMetrics().horizontalAdvance(minStr)) +
                   kPadding;

    // Vertical separator line
    p->setPen(selected ? textColor : opt.palette.mid().color());
    p->drawLine(anchorX, rect.top() + 2, anchorX, rect.bottom() - 2);

    // Min/max values
    p->setPen(selected ? textColor : opt.palette.placeholderText().color());
    QRect detailRect(anchorX + kPadding, rect.top(), detailsWidth, rect.height());
    p->drawText(detailRect, Qt::AlignTop, maxStr);
    p->drawText(detailRect, Qt::AlignBottom, minStr);
  }

  // Draw current value
  QRect valueRect = rect;
  valueRect.setLeft(anchorX + detailsWidth + kPadding);
  valueRect.setRight(rect.right() - getButtonsWidth() - kPadding);

  p->setFont(valueFont_);
  p->setPen(textColor);

  QString displayText = (item->valueWidth <= valueRect.width())
                            ? item->displayValue
                            : p->fontMetrics().elidedText(item->displayValue, Qt::ElideRight, valueRect.width());
  p->drawText(valueRect, Qt::AlignRight | Qt::AlignVCenter, displayText);

  // Draw action buttons
  paintButtons(p, opt, item, idx);
}

void SignalTreeDelegate::paintButtons(QPainter* p, const QStyleOptionViewItem& opt, SignalItem* item,
                                      const QModelIndex& idx) const {
  bool chartOpened = idx.data(IsChartedRole).toBool();

  auto drawButton = [&](int btnIdx, const QString& iconName, bool active) {
    QRect rect = buttonRect(opt.rect, btnIdx);
    bool hovered = (hoverIndex_ == idx && hoverButton_ == btnIdx);
    bool selected = opt.state & QStyle::State_Selected;

    if (hovered || active) {
      p->setRenderHint(QPainter::Antialiasing, true);

      QColor bg = active ? opt.palette.color(QPalette::Highlight) : opt.palette.color(QPalette::Button);
      bg.setAlpha(active ? 255 : 100);
      p->setBrush(bg);

      QPen borderPen;
      if (active && selected) {
        borderPen = QPen(opt.palette.color(QPalette::Base), 1.5);
      } else if (active) {
        borderPen = QPen(opt.palette.color(QPalette::Highlight).darker(150), 1);
      } else {
        borderPen = QPen(opt.palette.color(QPalette::Mid), 1);
      }
      p->setPen(borderPen);
      p->drawRoundedRect(rect.adjusted(1, 1, -1, -1), 4, 4);
      p->setRenderHint(QPainter::Antialiasing, false);
    }

    // Draw icon
    int iconPadding = 4;
    QSize iconSize(kBtnSize - (iconPadding * 2), kBtnSize - (iconPadding * 2));

    QColor iconColor;
    if (btnIdx == 0 && hovered) {
      iconColor = QColor(220, 53, 69);  // Red for remove button on hover
    } else {
      iconColor =
          (active || selected) ? opt.palette.color(QPalette::HighlightedText) : opt.palette.color(QPalette::Text);
    }

    QPixmap pix = utils::icon(iconName, iconSize, iconColor);
    p->drawPixmap(rect.left() + iconPadding, rect.top() + iconPadding, pix);
  };

  // 0: Remove button, 1: Plot button
  drawButton(0, "circle-minus", false);
  drawButton(1, chartOpened ? "chart-area" : "chart-line", chartOpened);
}

void SignalTreeDelegate::paintPropertyRow(QPainter* p, const QStyleOptionViewItem& opt, PropertyItem* item,
                                          const QModelIndex& idx) const {
  // For property rows, use default delegate painting
  QStyledItemDelegate::paint(p, opt, idx);
}

QRect SignalTreeDelegate::buttonRect(const QRect& colRect, int btnIdx) const {
  // btnIdx 0: Remove (far right), btnIdx 1: Plot (left of remove)
  int x = colRect.right() - kPadding - kBtnSize - (btnIdx * (kBtnSize + kBtnSpacing));
  int y = colRect.top() + (colRect.height() - kBtnSize) / 2;
  return QRect(x, y, kBtnSize, kBtnSize);
}

int SignalTreeDelegate::buttonAt(const QPoint& pos, const QRect& rect) const {
  for (int i = 0; i < 2; ++i) {
    if (buttonRect(rect, i).contains(pos)) return i;
  }
  return -1;
}

QWidget* SignalTreeDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                                          const QModelIndex& idx) const {
  auto* item = static_cast<TreeItem*>(idx.internalPointer());
  auto* propItem = dynamic_cast<PropertyItem*>(item);
  if (!propItem) return nullptr;

  if (propItem->property == SignalProperty::ValueTable) {
    ValueTableEditor dlg(propItem->sig->value_table, parent);
    dlg.setWindowTitle(propItem->sig->name);
    if (dlg.exec()) {
      const_cast<QAbstractItemModel*>(idx.model())->setData(idx, QVariant::fromValue(dlg.value_table));
    }
    return nullptr;
  }

  if (propItem->property == SignalProperty::SignalType) {
    auto* combo = new QComboBox(parent);
    combo->addItem(signalTypeToString(dbc::Signal::Type::Normal), static_cast<int>(dbc::Signal::Type::Normal));

    auto* msg = GetDBC()->msg(static_cast<const SignalTreeModel*>(idx.model())->messageId());
    if (!msg->multiplexor) {
      combo->addItem(signalTypeToString(dbc::Signal::Type::Multiplexor),
                     static_cast<int>(dbc::Signal::Type::Multiplexor));
    } else if (propItem->sig->type != dbc::Signal::Type::Multiplexor) {
      combo->addItem(signalTypeToString(dbc::Signal::Type::Multiplexed),
                     static_cast<int>(dbc::Signal::Type::Multiplexed));
    }
    return combo;
  }

  if (propItem->property == SignalProperty::Size) {
    auto* spinbox = new QSpinBox(parent);
    spinbox->setFrame(false);
    spinbox->setRange(1, MAX_CAN_LEN);
    return spinbox;
  }

  auto* edit = new QLineEdit(parent);
  edit->setFrame(false);

  if (propItem->property == SignalProperty::Name) {
    edit->setValidator(nameValidator_);
    auto* completer = new QCompleter(GetDBC()->signalNames(), edit);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    edit->setCompleter(completer);
  } else if (propItem->property == SignalProperty::Node) {
    edit->setValidator(nodeValidator_);
  } else {
    edit->setValidator(doubleValidator_);
  }
  return edit;
}

void SignalTreeDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const {
  auto* propItem = dynamic_cast<PropertyItem*>(static_cast<TreeItem*>(index.internalPointer()));
  if (propItem && propItem->property == SignalProperty::SignalType) {
    model->setData(index, static_cast<QComboBox*>(editor)->currentData().toInt());
    return;
  }
  QStyledItemDelegate::setModelData(editor, model, index);
}

bool SignalTreeDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option,
                                   const QModelIndex& index) {
  auto* item = static_cast<TreeItem*>(index.internalPointer());
  auto* sigItem = dynamic_cast<SignalItem*>(item);

  if (index.column() != 1 || !sigItem) {
    return QStyledItemDelegate::helpEvent(event, view, option, index);
  }

  // Button tooltips
  int btnIdx = buttonAt(event->pos(), option.rect);
  if (btnIdx != -1) {
    if (btnIdx == 1) {
      bool opened = index.data(IsChartedRole).toBool();
      QToolTip::showText(event->globalPos(),
                         opened ? tr("Close Plot") : tr("Show Plot\nSHIFT click to add to previous opened plot"), view);
    } else {
      QToolTip::showText(event->globalPos(), tr("Remove Signal"), view);
    }
    return true;
  }

  // Value tooltip
  if (!sigItem->sparkline->isEmpty()) {
    int sparkW = sigItem->sparkline->image().width() / sigItem->sparkline->image().devicePixelRatio();
    QRect valueRect = option.rect;
    valueRect.setLeft(option.rect.left() + sparkW + kPadding * 2);
    valueRect.setRight(option.rect.right() - getButtonsWidth());

    if (valueRect.contains(event->pos())) {
      QString tooltip = sigItem->displayValue + "\n" +
                        tr("Session Min: %1\nSession Max: %2")
                            .arg(QString::number(sigItem->sparkline->minVal(), 'f', 3),
                                 QString::number(sigItem->sparkline->maxVal(), 'f', 3));
      QToolTip::showText(event->globalPos(), tooltip, view);
      return true;
    }
  }

  return QStyledItemDelegate::helpEvent(event, view, option, index);
}

bool SignalTreeDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& opt,
                                     const QModelIndex& idx) {
  auto* item = static_cast<TreeItem*>(idx.internalPointer());
  auto* sigItem = dynamic_cast<SignalItem*>(item);

  if (!sigItem || idx.column() != 1) {
    return QStyledItemDelegate::editorEvent(event, model, opt, idx);
  }

  const auto type = event->type();
  if (type != QEvent::MouseMove && type != QEvent::MouseButtonPress && type != QEvent::MouseButtonRelease) {
    return QStyledItemDelegate::editorEvent(event, model, opt, idx);
  }

  const auto* mouseEvent = static_cast<QMouseEvent*>(event);
  const int btn = buttonAt(mouseEvent->pos(), opt.rect);

  if (type == QEvent::MouseMove) {
    if (hoverIndex_ != idx || hoverButton_ != btn) {
      QPersistentModelIndex oldIdx = hoverIndex_;
      hoverIndex_ = idx;
      hoverButton_ = btn;

      if (auto* view = qobject_cast<QAbstractItemView*>(const_cast<QWidget*>(opt.widget))) {
        if (oldIdx.isValid()) view->update(oldIdx);
        if (hoverIndex_.isValid()) view->update(hoverIndex_);
      }
    }
    return false;
  }

  if (btn != -1) {
    if (type == QEvent::MouseButtonRelease) {
      if (btn == 1) {
        bool isCharted = idx.data(IsChartedRole).toBool();
        emit plotRequested(sigItem->sig, !isCharted, mouseEvent->modifiers() & Qt::ShiftModifier);
      } else if (btn == 0) {
        clearHoverState();
        emit removeRequested(sigItem->sig);
      }
    }
    return true;  // Prevent row selection
  }

  return QStyledItemDelegate::editorEvent(event, model, opt, idx);
}

int SignalTreeDelegate::nameColumnWidth(const dbc::Signal* sig) const {
  int width = kPadding + kColorLabelW + kPadding;

  if (sig->type != dbc::Signal::Type::Normal) {
    QString badgeText = (sig->type == dbc::Signal::Type::Multiplexor) ? "M" : "m00";
    QFontMetrics badgeFm(labelFont_);
    width += badgeFm.horizontalAdvance(badgeText) + 8 + kPadding;
  }

  QFontMetrics nameFm(QApplication::font());
  return width + nameFm.horizontalAdvance(sig->name) + (kPadding * 2);
}

void SignalTreeDelegate::clearHoverState() {
  QPersistentModelIndex old = hoverIndex_;
  hoverIndex_ = QPersistentModelIndex();
  hoverButton_ = -1;
  if (old.isValid()) {
    if (auto* view = qobject_cast<QAbstractItemView*>(parent())) {
      view->update(old);
    }
  }
}
