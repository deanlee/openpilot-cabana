#include "signal_tree.h"

#include <QComboBox>
#include <QCompleter>
#include <QEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainterPath>
#include <QSpinBox>
#include <QToolTip>

#include "chart/chartswidget.h"
#include "commands.h"
#include "models/signal_tree.h"
#include "widgets/signalview.h"
#include "widgets/value_table_editor.h"

SignalTreeDelegate::SignalTreeDelegate(QObject* parent) : QStyledItemDelegate(parent) {
  name_validator = new NameValidator(this);
  node_validator = new QRegExpValidator(QRegExp("^\\w+(,\\w+)*$"), this);
  double_validator = new DoubleValidator(this);

  label_font.setPointSize(8);
  minmax_font.setPixelSize(10);
}

QSize SignalTreeDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
  auto item = (SignalTreeModel::Item*)index.internalPointer();
  int height = 0;
  if (item && item->type == SignalTreeModel::Item::Sig) {
    height = option.widget->style()->pixelMetric(QStyle::PM_ToolBarIconSize) + 4;
  } else {
    height = QStyledItemDelegate::sizeHint(option, index).height();
  }

  int width = option.widget->size().width() / 2;
  if (index.column() == 0) {
    int spacing = option.widget->style()->pixelMetric(QStyle::PM_TreeViewIndentation) + color_label_width + 8;
    auto text = index.data(Qt::DisplayRole).toString();
    if (item->type == SignalTreeModel::Item::Sig && item->sig->type != cabana::Signal::Type::Normal) {
      text += item->sig->type == cabana::Signal::Type::Multiplexor ? QString(" M ") : QString(" m%1 ").arg(item->sig->multiplex_value);
      spacing += (option.widget->style()->pixelMetric(QStyle::PM_FocusFrameHMargin) + 1) * 2;
    }
    width = std::min<int>(option.widget->size().width() / 3.0, option.fontMetrics.horizontalAdvance(text) + spacing);
  }
  return {width, height};
}

void SignalTreeDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const {
  auto item = (SignalTreeModel::Item*)index.internalPointer();
  if (editor && item->type == SignalTreeModel::Item::Sig && index.column() == 1) {
    QRect geom = option.rect;
    geom.setLeft(geom.right() - editor->sizeHint().width());
    editor->setGeometry(geom);
    button_size = geom.size();
    return;
  }
  QStyledItemDelegate::updateEditorGeometry(editor, option, index);
}

void SignalTreeDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
  const int h_margin = option.widget->style()->pixelMetric(QStyle::PM_FocusFrameHMargin) + 1;
  const int v_margin = option.widget->style()->pixelMetric(QStyle::PM_FocusFrameVMargin);
  auto item = static_cast<SignalTreeModel::Item*>(index.internalPointer());

  QRect rect = option.rect.adjusted(h_margin, v_margin, -h_margin, -v_margin);
  painter->setRenderHint(QPainter::Antialiasing);
  if (option.state & QStyle::State_Selected) {
    painter->fillRect(option.rect, option.palette.brush(QPalette::Normal, QPalette::Highlight));
  }

  if (index.column() == 0) {
    if (item->type == SignalTreeModel::Item::Sig) {
      // color label
      QPainterPath path;
      QRect icon_rect{rect.x(), rect.y(), color_label_width, rect.height()};
      path.addRoundedRect(icon_rect, 3, 3);
      painter->setPen(item->highlight ? Qt::white : Qt::black);
      painter->setFont(label_font);
      painter->fillPath(path, item->sig->color.darker(item->highlight ? 125 : 0));
      painter->drawText(icon_rect, Qt::AlignCenter, QString::number(item->row() + 1));

      rect.setLeft(icon_rect.right() + h_margin * 2);
      // multiplexer indicator
      if (item->sig->type != cabana::Signal::Type::Normal) {
        QString indicator = item->sig->type == cabana::Signal::Type::Multiplexor ? QString(" M ") : QString(" m%1 ").arg(item->sig->multiplex_value);
        QRect indicator_rect{rect.x(), rect.y(), option.fontMetrics.horizontalAdvance(indicator), rect.height()};
        painter->setBrush(Qt::gray);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(indicator_rect, 3, 3);
        painter->setPen(Qt::white);
        painter->drawText(indicator_rect, Qt::AlignCenter, indicator);
        rect.setLeft(indicator_rect.right() + h_margin * 2);
      }
    } else {
      rect.setLeft(option.widget->style()->pixelMetric(QStyle::PM_TreeViewIndentation) + color_label_width + h_margin * 3);
    }

    // name
    auto text = option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, rect.width());
    painter->setPen(option.palette.color(option.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text));
    painter->setFont(option.font);
    painter->drawText(rect, option.displayAlignment, text);
  } else if (index.column() == 1) {
    const int btn_space = getButtonsWidth();

    // Create a working rect that EXCLUDES the button area
    QRect contentRect = rect;
    contentRect.setRight(rect.right() - btn_space);

    if (!item->sparkline.pixmap.isNull()) {
      QSize sparkline_size = item->sparkline.pixmap.size() / item->sparkline.pixmap.devicePixelRatio();

      // Draw sparkline inside the content area
      painter->drawPixmap(QRect(contentRect.topLeft(), sparkline_size), item->sparkline.pixmap);

      // Adjust for min-max/freq text
      painter->setPen(option.palette.color(option.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text));

      int value_adjust = 10;
      QRect textRect = contentRect;
      textRect.adjust(sparkline_size.width() + 1, 0, 0, 0);

      if (!item->sparkline.isEmpty() && (item->highlight || option.state & QStyle::State_Selected)) {
        painter->drawLine(textRect.topLeft(), textRect.bottomLeft());
        textRect.adjust(5, -v_margin, 0, v_margin);
        painter->setFont(minmax_font);

        QString min = QString::number(item->sparkline.min_val);
        QString max = QString::number(item->sparkline.max_val);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignTop, max);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignBottom, min);

        value_adjust = std::max(QFontMetrics(minmax_font).horizontalAdvance(min),
                                QFontMetrics(minmax_font).horizontalAdvance(max)) + 10;
      } else if (!item->sparkline.isEmpty() && item->sig->type == cabana::Signal::Type::Multiplexed) {
        painter->setFont(label_font);
        QString freq = QString("%1 hz").arg(item->sparkline.freq(), 0, 'g', 2);
        painter->drawText(textRect.adjusted(5, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, freq);
        value_adjust = QFontMetrics(label_font).horizontalAdvance(freq) + 15;
      }

      // Draw Signal Value text
      painter->setFont(option.font);
      // This rect is now bounded by the sparkline on the left and the buttons on the right
      textRect.adjust(value_adjust, 0, 0, 0);
      auto text = option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRect.width());
      painter->drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, text);

      // Draw Buttons (uses the original rect to stay pinned to the absolute right)
      if (item->type == SignalTreeModel::Item::Sig) {
        drawButtons(painter, option, index, item);
      }
    } else {
      QStyledItemDelegate::paint(painter, option, index);
    }
  }
}

QWidget* SignalTreeDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const {
  auto item = (SignalTreeModel::Item*)index.internalPointer();
  if (item->type == SignalTreeModel::Item::Name || item->type == SignalTreeModel::Item::Node || item->type == SignalTreeModel::Item::Offset ||
      item->type == SignalTreeModel::Item::Factor || item->type == SignalTreeModel::Item::MultiplexValue ||
      item->type == SignalTreeModel::Item::Min || item->type == SignalTreeModel::Item::Max) {
    QLineEdit* e = new QLineEdit(parent);
    e->setFrame(false);
    if (item->type == SignalTreeModel::Item::Name)
      e->setValidator(name_validator);
    else if (item->type == SignalTreeModel::Item::Node)
      e->setValidator(node_validator);
    else
      e->setValidator(double_validator);

    if (item->type == SignalTreeModel::Item::Name) {
      QCompleter* completer = new QCompleter(dbc()->signalNames(), e);
      completer->setCaseSensitivity(Qt::CaseInsensitive);
      completer->setFilterMode(Qt::MatchContains);
      e->setCompleter(completer);
    }
    return e;
  } else if (item->type == SignalTreeModel::Item::Size) {
    QSpinBox* spin = new QSpinBox(parent);
    spin->setFrame(false);
    spin->setRange(1, CAN_MAX_DATA_BYTES);
    return spin;
  } else if (item->type == SignalTreeModel::Item::SignalType) {
    QComboBox* c = new QComboBox(parent);
    c->addItem(signalTypeToString(cabana::Signal::Type::Normal), (int)cabana::Signal::Type::Normal);
    if (!dbc()->msg(((SignalTreeModel*)index.model())->msg_id)->multiplexor) {
      c->addItem(signalTypeToString(cabana::Signal::Type::Multiplexor), (int)cabana::Signal::Type::Multiplexor);
    } else if (item->sig->type != cabana::Signal::Type::Multiplexor) {
      c->addItem(signalTypeToString(cabana::Signal::Type::Multiplexed), (int)cabana::Signal::Type::Multiplexed);
    }
    return c;
  } else if (item->type == SignalTreeModel::Item::Desc) {
    ValueTableEditor dlg(item->sig->val_desc, parent);
    dlg.setWindowTitle(item->sig->name);
    if (dlg.exec()) {
      ((QAbstractItemModel*)index.model())->setData(index, QVariant::fromValue(dlg.val_desc));
    }
    return nullptr;
  }
  return QStyledItemDelegate::createEditor(parent, option, index);
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

QRect SignalTreeDelegate::getButtonRect(const QRect& columnRect, int buttonIndex) const {
  int x = columnRect.right() - ((buttonIndex + 1) * BTN_WIDTH) - (buttonIndex * BTN_SPACING) - 5;
  int y = columnRect.top() + (columnRect.height() - BTN_WIDTH) / 2;
  return QRect(x, y, BTN_WIDTH, BTN_WIDTH);
}

void SignalTreeDelegate::drawButtons(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx, SignalTreeModel::Item* item) const {
  auto model = static_cast<const SignalTreeModel*>(idx.model());
  bool chart_opened = static_cast<SignalView*>(parent())->charts->hasSignal(model->msg_id, item->sig);

  auto drawBtn = [&](int btnIdx, const QString& iconName, bool active) {
    QRect rect = getButtonRect(opt.rect, btnIdx);
    bool hovered = (hoverIndex == idx && hoverButton == btnIdx);

    if (hovered || active) {
      p->save();
      p->setRenderHint(QPainter::Antialiasing);

      // Background: Highlight if active, very light overlay if hovered
      QColor bg = active ? opt.palette.color(QPalette::Highlight) : opt.palette.color(QPalette::Button);
      p->setBrush(bg);

      // Border: Use a darker shade to create the "raised" or "defined" look
      p->setPen(opt.palette.color(active ? QPalette::Highlight : QPalette::Mid));

      p->setOpacity(active ? 1.0 : 0.5);
      p->drawRoundedRect(rect.adjusted(1, 1, -1, -1), 3, 3);
      p->restore();
    }

    double dpr = p->device()->devicePixelRatioF();
    QSize logicalSize(BTN_WIDTH - 6, BTN_WIDTH - 6);
    QSize physicalSize = logicalSize * dpr;

    QPixmap pix = utils::icon(iconName, physicalSize);
    pix.setDevicePixelRatio(dpr);
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    p->setRenderHint(QPainter::Antialiasing);
    p->drawPixmap(rect.adjusted(3, 3, -3, -3), pix);
  };

  drawBtn(0, "circle-minus", false);
  drawBtn(1, "chart-line", chart_opened);
}

bool SignalTreeDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index) {
  auto item = static_cast<SignalTreeModel::Item*>(index.internalPointer());
  int btnIdx = (index.column() == 1 && item->type == SignalTreeModel::Item::Sig) ? buttonAt(event->pos(), option.rect) : -1;

  if (btnIdx != -1) {
    SignalView* sigView = qobject_cast<SignalView*>(view->parentWidget()); 
    if (sigView) {
      if (btnIdx == 1) { // Plot Button
        auto model = static_cast<const SignalTreeModel*>(index.model());
        bool opened = sigView->charts->hasSignal(model->msg_id, item->sig);
        QToolTip::showText(event->globalPos(), opened ? tr("Close Plot") : tr("Show Plot\nSHIFT click to add to previous opened plot"), view);
      } else { // Remove Button
        QToolTip::showText(event->globalPos(), tr("Remove Signal"), view);
      }
      return true;
    }
  }

  QToolTip::hideText();
  return QStyledItemDelegate::helpEvent(event, view, option, index);
}

bool SignalTreeDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) {
  auto item = static_cast<SignalTreeModel::Item*>(index.internalPointer());
  if (!item) return false;

  QMouseEvent* e = static_cast<QMouseEvent*>(event);

  // 1. Hover Logic
  if (event->type() == QEvent::MouseMove) {
    int btn = (index.column() == 1 && item->type == SignalTreeModel::Item::Sig) ? buttonAt(e->pos(), option.rect) : -1;
    if (hoverIndex != index || hoverButton != btn) {
      hoverIndex = index;
      hoverButton = btn;
      const_cast<QWidget*>(option.widget)->update();
    }
    return false;
  }

  // 2. Click Logic
  if (event->type() == QEvent::MouseButtonRelease) {
    int btn = (index.column() == 1 && item->type == SignalTreeModel::Item::Sig) ? buttonAt(e->pos(), option.rect) : -1;

    if (btn != -1) {
      // It's a button click on a Signal item
      SignalView* view = static_cast<SignalView*>(parent());
      MessageId msg_id = static_cast<SignalTreeModel*>(model)->msg_id;
      if (btn == 1) {
        bool opened = view->charts->hasSignal(msg_id, item->sig);
        emit view->showChart(msg_id, item->sig, !opened, e->modifiers() & Qt::ShiftModifier);
      } else {
        UndoStack::push(new RemoveSigCommand(msg_id, item->sig));
      }
      return true;  // Mark event handled so 'clicked()' isn't fired
    } else {
      // It's a click anywhere else: text, child items, or extra info
      auto tree = qobject_cast<QTreeView*>(const_cast<QWidget*>(option.widget));
      if (tree && (item->type == SignalTreeModel::Item::Sig || item->type == SignalTreeModel::Item::ExtraInfo)) {
        auto expand_idx = index.siblingAtColumn(0);
        tree->setExpanded(expand_idx, !tree->isExpanded(expand_idx));
        return true;
      }
    }
  }

  return QStyledItemDelegate::editorEvent(event, model, option, index);
}

void SignalTreeDelegate::clearHoverState() {
  hoverIndex = QModelIndex();
  hoverButton = -1;
}
