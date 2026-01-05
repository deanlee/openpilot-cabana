#include "signal_tree.h"

#include <QCompleter>
#include <QComboBox>
#include <QLineEdit>
#include <QPainterPath>
#include <QSpinBox>

#include "models/signal_tree.h"
#include "widgets/value_table_editor.h"

SignalTreeDelegate::SignalTreeDelegate(QObject *parent) : QStyledItemDelegate(parent) {
  name_validator = new NameValidator(this);
  node_validator = new QRegExpValidator(QRegExp("^\\w+(,\\w+)*$"), this);
  double_validator = new DoubleValidator(this);

  label_font.setPointSize(8);
  minmax_font.setPixelSize(10);
}

QSize SignalTreeDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
  int width = option.widget->size().width() / 2;
  if (index.column() == 0) {
    int spacing = option.widget->style()->pixelMetric(QStyle::PM_TreeViewIndentation) + color_label_width + 8;
    auto text = index.data(Qt::DisplayRole).toString();
    auto item = (SignalTreeModel::Item *)index.internalPointer();
    if (item->type == SignalTreeModel::Item::Sig && item->sig->type != cabana::Signal::Type::Normal) {
      text += item->sig->type == cabana::Signal::Type::Multiplexor ? QString(" M ") : QString(" m%1 ").arg(item->sig->multiplex_value);
      spacing += (option.widget->style()->pixelMetric(QStyle::PM_FocusFrameHMargin) + 1) * 2;
    }
    width = std::min<int>(option.widget->size().width() / 3.0, option.fontMetrics.horizontalAdvance(text) + spacing);
  }
  return {width, option.fontMetrics.height() + option.widget->style()->pixelMetric(QStyle::PM_FocusFrameVMargin) * 2};
}

void SignalTreeDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const {
  auto item = (SignalTreeModel::Item *)index.internalPointer();
  if (editor && item->type == SignalTreeModel::Item::Sig && index.column() == 1) {
    QRect geom = option.rect;
    geom.setLeft(geom.right() - editor->sizeHint().width());
    editor->setGeometry(geom);
    button_size = geom.size();
    return;
  }
  QStyledItemDelegate::updateEditorGeometry(editor, option, index);
}

void SignalTreeDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
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
    if (!item->sparkline.pixmap.isNull()) {
      QSize sparkline_size = item->sparkline.pixmap.size() / item->sparkline.pixmap.devicePixelRatio();
      painter->drawPixmap(QRect(rect.topLeft(), sparkline_size), item->sparkline.pixmap);
      // min-max value
      painter->setPen(option.palette.color(option.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text));
      rect.adjust(sparkline_size.width() + 1, 0, 0, 0);
      int value_adjust = 10;
      if (!item->sparkline.isEmpty() && (item->highlight || option.state & QStyle::State_Selected)) {
        painter->drawLine(rect.topLeft(), rect.bottomLeft());
        rect.adjust(5, -v_margin, 0, v_margin);
        painter->setFont(minmax_font);
        QString min = QString::number(item->sparkline.min_val);
        QString max = QString::number(item->sparkline.max_val);
        painter->drawText(rect, Qt::AlignLeft | Qt::AlignTop, max);
        painter->drawText(rect, Qt::AlignLeft | Qt::AlignBottom, min);
        QFontMetrics fm(minmax_font);
        value_adjust = std::max(fm.horizontalAdvance(min), fm.horizontalAdvance(max)) + 5;
      } else if (!item->sparkline.isEmpty() && item->sig->type == cabana::Signal::Type::Multiplexed) {
        // display freq of multiplexed signal
        painter->setFont(label_font);
        QString freq = QString("%1 hz").arg(item->sparkline.freq(), 0, 'g', 2);
        painter->drawText(rect.adjusted(5, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, freq);
        value_adjust = QFontMetrics(label_font).horizontalAdvance(freq) + 10;
      }
      // signal value
      painter->setFont(option.font);
      rect.adjust(value_adjust, 0, -button_size.width(), 0);
      auto text = option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, rect.width());
      painter->drawText(rect, Qt::AlignRight | Qt::AlignVCenter, text);
    } else {
      QStyledItemDelegate::paint(painter, option, index);
    }
  }
}

QWidget *SignalTreeDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
  auto item = (SignalTreeModel::Item *)index.internalPointer();
  if (item->type == SignalTreeModel::Item::Name || item->type == SignalTreeModel::Item::Node || item->type == SignalTreeModel::Item::Offset ||
      item->type == SignalTreeModel::Item::Factor || item->type == SignalTreeModel::Item::MultiplexValue ||
      item->type == SignalTreeModel::Item::Min || item->type == SignalTreeModel::Item::Max) {
    QLineEdit *e = new QLineEdit(parent);
    e->setFrame(false);
    if (item->type == SignalTreeModel::Item::Name) e->setValidator(name_validator);
    else if (item->type == SignalTreeModel::Item::Node) e->setValidator(node_validator);
    else e->setValidator(double_validator);

    if (item->type == SignalTreeModel::Item::Name) {
      QCompleter *completer = new QCompleter(dbc()->signalNames(), e);
      completer->setCaseSensitivity(Qt::CaseInsensitive);
      completer->setFilterMode(Qt::MatchContains);
      e->setCompleter(completer);
    }
    return e;
  } else if (item->type == SignalTreeModel::Item::Size) {
    QSpinBox *spin = new QSpinBox(parent);
    spin->setFrame(false);
    spin->setRange(1, CAN_MAX_DATA_BYTES);
    return spin;
  } else if (item->type == SignalTreeModel::Item::SignalType) {
    QComboBox *c = new QComboBox(parent);
    c->addItem(signalTypeToString(cabana::Signal::Type::Normal), (int)cabana::Signal::Type::Normal);
    if (!dbc()->msg(((SignalTreeModel *)index.model())->msg_id)->multiplexor) {
      c->addItem(signalTypeToString(cabana::Signal::Type::Multiplexor), (int)cabana::Signal::Type::Multiplexor);
    } else if (item->sig->type != cabana::Signal::Type::Multiplexor) {
      c->addItem(signalTypeToString(cabana::Signal::Type::Multiplexed), (int)cabana::Signal::Type::Multiplexed);
    }
    return c;
  } else if (item->type == SignalTreeModel::Item::Desc) {
    ValueTableEditor dlg(item->sig->val_desc, parent);
    dlg.setWindowTitle(item->sig->name);
    if (dlg.exec()) {
      ((QAbstractItemModel *)index.model())->setData(index, QVariant::fromValue(dlg.val_desc));
    }
    return nullptr;
  }
  return QStyledItemDelegate::createEditor(parent, option, index);
}

void SignalTreeDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const {
  auto item = (SignalTreeModel::Item *)index.internalPointer();
  if (item->type == SignalTreeModel::Item::SignalType) {
    model->setData(index, ((QComboBox*)editor)->currentData().toInt());
    return;
  }
  QStyledItemDelegate::setModelData(editor, model, index);
}
