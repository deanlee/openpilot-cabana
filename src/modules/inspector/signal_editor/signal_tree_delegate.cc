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
#include "modules/charts/charts_panel.h"
#include "signal_editor.h"
#include "signal_tree_model.h"
#include "value_table_editor.h"
#include "widgets/validators.h"

SignalTreeDelegate::SignalTreeDelegate(QObject* parent) : QStyledItemDelegate(parent) {
  name_validator = new NameValidator(this);
  node_validator = new QRegExpValidator(QRegExp("^\\w+(,\\w+)*$"), this);
  double_validator = new DoubleValidator(this);

  label_font.setPointSize(8);
  minmax_font.setPixelSize(10);
}

QSize SignalTreeDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
  // Use toolbar icon size + padding for row height; width determined by header
  int height = option.widget->style()->pixelMetric(QStyle::PM_ToolBarIconSize) + kPadding;
  return QSize(-1, height);
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
  const bool show_details = (sel || hoverIndex == idx) && !item->sparkline.isEmpty();
  const QColor text_c = opt.palette.color(sel ? QPalette::HighlightedText : QPalette::Text);

  // Sparkline
  int sparkW = 0;
  if (!item->sparkline.image.isNull()) {
    const auto& img = item->sparkline.image;
    sparkW = img.width() / img.devicePixelRatio();
    item->sparkline.setHighlight(sel);
    p->drawImage(r.left(), r.top() + (r.height() - img.height() / img.devicePixelRatio()) / 2, img);
  }

  // Details (Min/Max)
  int detailsW = 0;
  int anchorX = r.left() + sparkW;
  if (show_details) {
    p->setFont(minmax_font);
    QString maxS = QString::number(item->sparkline.max_val, 'f', 1);
    QString minS = QString::number(item->sparkline.min_val, 'f', 1);
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

  p->setFont(opt.font);
  p->setPen(text_c);
  p->drawText(valR, Qt::AlignRight | Qt::AlignVCenter, opt.fontMetrics.elidedText(item->sig_val, Qt::ElideRight, valR.width()));

  drawButtons(p, opt, item, idx);
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
      QCompleter* completer = new QCompleter(GetDBC()->signalNames(), e);
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
    c->addItem(signalTypeToString(dbc::Signal::Type::Normal), (int)dbc::Signal::Type::Normal);
    if (!GetDBC()->msg(((SignalTreeModel*)index.model())->msg_id)->multiplexor) {
      c->addItem(signalTypeToString(dbc::Signal::Type::Multiplexor), (int)dbc::Signal::Type::Multiplexor);
    } else if (item->sig->type != dbc::Signal::Type::Multiplexor) {
      c->addItem(signalTypeToString(dbc::Signal::Type::Multiplexed), (int)dbc::Signal::Type::Multiplexed);
    }
    return c;
  } else if (item->type == SignalTreeModel::Item::ValueTable) {
    ValueTableEditor dlg(item->sig->value_table, parent);
    dlg.setWindowTitle(item->sig->name);
    if (dlg.exec()) {
      ((QAbstractItemModel*)index.model())->setData(index, QVariant::fromValue(dlg.value_table));
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

    if (hovered || active) {
      // Background: Highlight if active, light overlay if hovered
      QColor bg = active ? opt.palette.color(QPalette::Highlight) : opt.palette.color(QPalette::Button);
      p->setOpacity(active ? 1.0 : 0.4);
      p->setBrush(bg);
      p->setPen(opt.palette.color(active ? QPalette::Highlight : QPalette::Mid));
      p->drawRoundedRect(rect.adjusted(1, 1, -1, -1), 4, 4);
    }

    // Icon rendering logic
    double dpr = p->device()->devicePixelRatioF();
    // Padding inside the button for the icon
    int iconPadding = 4;
    QSize iconSize = QSize(kBtnSize - (iconPadding * 2), kBtnSize - (iconPadding * 2));

    QColor icon_color = (active || (opt.state & QStyle::State_Selected))
                            ? opt.palette.color(QPalette::HighlightedText)
                            : opt.palette.color(QPalette::Text);

    QPixmap pix = utils::icon(iconName, iconSize * dpr, icon_color);
    pix.setDevicePixelRatio(dpr);

    p->setOpacity(1.0);  // Reset opacity for the icon
    p->drawPixmap(rect.left() + iconPadding, rect.top() + iconPadding, pix);
  };

  // 0: Remove, 1: Plot
  drawBtn(0, "circle-minus", false);
  drawBtn(1, chart_opened ? "chart-area" : "chart-line", chart_opened);
}

bool SignalTreeDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index) {
  if (index.column() == 0) {
   return QStyledItemDelegate::helpEvent(event, view, option, index);
  }

  auto item = static_cast<SignalTreeModel::Item*>(index.internalPointer());
  int btnIdx = (index.column() == 1 && item->type == SignalTreeModel::Item::Sig) ? buttonAt(event->pos(), option.rect) : -1;

  if (btnIdx != -1) {
    SignalEditor* sigView = qobject_cast<SignalEditor*>(view->parentWidget());
    if (sigView) {
      if (btnIdx == 1) {  // Plot Button
        bool opened = index.data(IsChartedRole).toBool();
        QToolTip::showText(event->globalPos(), opened ? tr("Close Plot") : tr("Show Plot\nSHIFT click to add to previous opened plot"), view);
      } else {  // Remove Button
        QToolTip::showText(event->globalPos(), tr("Remove Signal"), view);
      }
      return true;
    }
  } else {
    int right_edge = option.rect.right() - getButtonsWidth();
    QRect value_rect = option.rect;
    value_rect.setLeft(right_edge - kValueWidth);
    value_rect.setRight(right_edge);
    if (value_rect.contains(event->pos()) && item && !item->sig_val.isEmpty()) {
      QString tooltip = item->sig_val + "\n\n" +
                        tr("Min: %1\nMax: %2").arg(QString::number(item->sparkline.min_val, 'f', 3),
                                                 QString::number(item->sparkline.max_val, 'f', 3));
      QToolTip::showText(event->globalPos(), tooltip, view);
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

  // Handle Leave Event
  if (event->type() == QEvent::Leave) {
    if (hoverIndex.isValid()) {
      hoverIndex = QModelIndex();
      hoverButton = -1;
      const_cast<QWidget*>(option.widget)->update();
    }
    return false;
  }

  // 1. Hover Logic
  if (event->type() == QEvent::MouseMove) {
    int btn = (index.column() == 1 && item->type == SignalTreeModel::Item::Sig) ? buttonAt(e->pos(), option.rect) : -1;
    if (hoverIndex != index || hoverButton != btn) {
      hoverIndex = index;
      hoverButton = btn;
      qobject_cast<QAbstractItemView*>(const_cast<QWidget*>(option.widget))->viewport()->update();
    }
    return false;
  }

  // 2. Click Logic
  if (event->type() == QEvent::MouseButtonRelease) {
    int btn = (index.column() == 1 && item->type == SignalTreeModel::Item::Sig) ? buttonAt(e->pos(), option.rect) : -1;

    if (btn != -1) {
      // It's a button click on a Signal item
      SignalEditor* view = static_cast<SignalEditor*>(parent());
      MessageId msg_id = static_cast<SignalTreeModel*>(model)->msg_id;
      if (btn == 1) {
        bool opened = index.data(IsChartedRole).toBool();
        emit view->showChart(msg_id, item->sig, !opened, e->modifiers() & Qt::ShiftModifier);
      } else {
        UndoStack::push(new RemoveSigCommand(msg_id, item->sig));
      }
      return true;  // Mark event handled so 'clicked()' isn't fired
    }
  }

  return QStyledItemDelegate::editorEvent(event, model, option, index);
}

int SignalTreeDelegate::nameColumnWidth(const dbc::Signal* sig) const {
  // Use the default font for the name text, and label_font for badges
  QFontMetrics nameFm(QApplication::font());
  QFontMetrics badgeFm(label_font);

  int width = kPadding;

  width += kColorLabelW + kPadding;

  if (sig->type != dbc::Signal::Type::Normal) {
    QString m_text = (sig->type == dbc::Signal::Type::Multiplexor) ? "M" : "m00";
    width += badgeFm.horizontalAdvance(m_text) + 8 + kPadding;
  }

  width += nameFm.horizontalAdvance(sig->name);

  width += kPadding * 2;

  return width;
}
