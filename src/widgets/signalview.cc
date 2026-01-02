#include "signalview.h"

#include <algorithm>

#include <QCompleter>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QtConcurrent>
#include <QVBoxLayout>

#include "commands.h"

// SignalItemDelegate

SignalItemDelegate::SignalItemDelegate(QObject *parent) : QStyledItemDelegate(parent) {
  name_validator = new NameValidator(this);
  node_validator = new QRegExpValidator(QRegExp("^\\w+(,\\w+)*$"), this);
  double_validator = new DoubleValidator(this);

  label_font.setPointSize(8);
  minmax_font.setPixelSize(10);
}

QSize SignalItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
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

void SignalItemDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const {
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

void SignalItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
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

QWidget *SignalItemDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
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
    ValueDescriptionDlg dlg(item->sig->val_desc, parent);
    dlg.setWindowTitle(item->sig->name);
    if (dlg.exec()) {
      ((QAbstractItemModel *)index.model())->setData(index, QVariant::fromValue(dlg.val_desc));
    }
    return nullptr;
  }
  return QStyledItemDelegate::createEditor(parent, option, index);
}

void SignalItemDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const {
  auto item = (SignalTreeModel::Item *)index.internalPointer();
  if (item->type == SignalTreeModel::Item::SignalType) {
    model->setData(index, ((QComboBox*)editor)->currentData().toInt());
    return;
  }
  QStyledItemDelegate::setModelData(editor, model, index);
}

// SignalView

SignalView::SignalView(ChartsWidget *charts, QWidget *parent) : charts(charts), QFrame(parent) {
  setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  // title bar
  QWidget *title_bar = new QWidget(this);
  QHBoxLayout *hl = new QHBoxLayout(title_bar);
  hl->addWidget(signal_count_lb = new QLabel());
  filter_edit = new QLineEdit(this);
  QRegularExpression re("\\S+");
  filter_edit->setValidator(new QRegularExpressionValidator(re, this));
  filter_edit->setClearButtonEnabled(true);
  filter_edit->setPlaceholderText(tr("Filter Signal"));
  hl->addWidget(filter_edit);
  hl->addStretch(1);

  // WARNING: increasing the maximum range can result in severe performance degradation.
  // 30s is a reasonable value at present.
  const int max_range = 30; // 30s
  settings.sparkline_range = std::clamp(settings.sparkline_range, 1, max_range);
  hl->addWidget(sparkline_label = new QLabel());
  hl->addWidget(sparkline_range_slider = new QSlider(Qt::Horizontal, this));
  sparkline_range_slider->setRange(1, max_range);
  sparkline_range_slider->setValue(settings.sparkline_range);
  sparkline_range_slider->setToolTip(tr("Sparkline time range"));

  auto collapse_btn = new ToolButton("dash-square", tr("Collapse All"));
  collapse_btn->setIconSize({12, 12});
  hl->addWidget(collapse_btn);

  // tree view
  tree = new TreeView(this);
  tree->setModel(model = new SignalTreeModel(this));
  tree->setItemDelegate(delegate = new SignalItemDelegate(this));
  tree->setFrameShape(QFrame::NoFrame);
  tree->setHeaderHidden(true);
  tree->setMouseTracking(true);
  tree->setExpandsOnDoubleClick(false);
  tree->setEditTriggers(QAbstractItemView::AllEditTriggers);
  tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  tree->header()->setStretchLastSection(true);
  tree->setMinimumHeight(300);

  // Use a distinctive background for the whole row containing a QSpinBox or QLineEdit
  QString nodeBgColor = palette().color(QPalette::AlternateBase).name(QColor::HexArgb);
  tree->setStyleSheet(QString("QSpinBox{background-color:%1;border:none;} QLineEdit{background-color:%1;}").arg(nodeBgColor));

  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(0);
  main_layout->addWidget(title_bar);
  main_layout->addWidget(tree);
  updateToolBar();

  connect(filter_edit, &QLineEdit::textEdited, model, &SignalTreeModel::setFilter);
  connect(sparkline_range_slider, &QSlider::valueChanged, this, &SignalView::setSparklineRange);
  connect(collapse_btn, &QPushButton::clicked, tree, &QTreeView::collapseAll);
  connect(tree, &QAbstractItemView::clicked, this, &SignalView::rowClicked);
  connect(tree, &QTreeView::viewportEntered, [this]() { emit highlight(nullptr); });
  connect(tree, &QTreeView::entered, [this](const QModelIndex &index) { emit highlight(model->getItem(index)->sig); });
  connect(model, &QAbstractItemModel::modelReset, this, &SignalView::rowsChanged);
  connect(model, &QAbstractItemModel::rowsRemoved, this, &SignalView::rowsChanged);
  connect(dbc(), &DBCManager::signalAdded, this, &SignalView::handleSignalAdded);
  connect(dbc(), &DBCManager::signalUpdated, this, &SignalView::handleSignalUpdated);
  connect(tree->verticalScrollBar(), &QScrollBar::valueChanged, [this]() { updateState(); });
  connect(tree->verticalScrollBar(), &QScrollBar::rangeChanged, [this]() { updateState(); });
  connect(can, &AbstractStream::snapshotsUpdated, this, &SignalView::updateState);
  connect(tree->header(), &QHeaderView::sectionResized, [this](int logicalIndex, int oldSize, int newSize) {
    if (logicalIndex == 1) {
      value_column_width = newSize;
      updateState();
    }
  });

  setWhatsThis(tr(R"(
    <b>Signal view</b><br />
    <!-- TODO: add descprition here -->
  )"));
}

void SignalView::setMessage(const MessageId &id) {
  max_value_width = 0;
  filter_edit->clear();
  model->setMessage(id);
}

void SignalView::rowsChanged() {
  for (int i = 0; i < model->rowCount(); ++i) {
    auto index = model->index(i, 1);
    if (!tree->indexWidget(index)) {
      QWidget *w = new QWidget(this);
      QHBoxLayout *h = new QHBoxLayout(w);
      int v_margin = style()->pixelMetric(QStyle::PM_FocusFrameVMargin);
      int h_margin = style()->pixelMetric(QStyle::PM_FocusFrameHMargin);
      h->setContentsMargins(0, v_margin, -h_margin, v_margin);
      h->setSpacing(style()->pixelMetric(QStyle::PM_ToolBarItemSpacing));

      auto remove_btn = new ToolButton("x", tr("Remove signal"));
      auto plot_btn = new ToolButton("graph-up", "");
      plot_btn->setCheckable(true);
      h->addWidget(plot_btn);
      h->addWidget(remove_btn);

      tree->setIndexWidget(index, w);
      auto sig = model->getItem(index)->sig;
      connect(remove_btn, &QToolButton::clicked, [=]() { UndoStack::push(new RemoveSigCommand(model->msg_id, sig)); });
      connect(plot_btn, &QToolButton::clicked, [=](bool checked) {
        emit showChart(model->msg_id, sig, checked, QGuiApplication::keyboardModifiers() & Qt::ShiftModifier);
      });
    }
  }
  updateToolBar();
  updateChartState();
  updateState();
}

void SignalView::rowClicked(const QModelIndex &index) {
  auto item = model->getItem(index);
  if (item->type == SignalTreeModel::Item::Sig || item->type == SignalTreeModel::Item::ExtraInfo) {
    auto expand_index = model->index(index.row(), 0, index.parent());
    tree->setExpanded(expand_index, !tree->isExpanded(expand_index));
  }
}

void SignalView::selectSignal(const cabana::Signal *sig, bool expand) {
  if (int row = model->signalRow(sig); row != -1) {
    auto idx = model->index(row, 0);
    if (expand) {
      tree->setExpanded(idx, !tree->isExpanded(idx));
    }
    tree->scrollTo(idx, QAbstractItemView::PositionAtTop);
    tree->setCurrentIndex(idx);
  }
}

void SignalView::updateChartState() {
  int i = 0;
  for (auto item : model->root->children) {
    bool chart_opened = charts->hasSignal(model->msg_id, item->sig);
    auto buttons = tree->indexWidget(model->index(i, 1))->findChildren<QToolButton *>();
    if (buttons.size() > 0) {
      buttons[0]->setChecked(chart_opened);
      buttons[0]->setToolTip(chart_opened ? tr("Close Plot") : tr("Show Plot\nSHIFT click to add to previous opened plot"));
    }
    ++i;
  }
}

void SignalView::signalHovered(const cabana::Signal *sig) {
  auto &children = model->root->children;
  for (int i = 0; i < children.size(); ++i) {
    bool highlight = children[i]->sig == sig;
    if (std::exchange(children[i]->highlight, highlight) != highlight) {
      emit model->dataChanged(model->index(i, 0), model->index(i, 0), {Qt::DecorationRole});
      emit model->dataChanged(model->index(i, 1), model->index(i, 1), {Qt::DisplayRole});
    }
  }
}

void SignalView::updateToolBar() {
  signal_count_lb->setText(tr("Signals: %1").arg(model->rowCount()));
  sparkline_label->setText(utils::formatSeconds(settings.sparkline_range));
}

void SignalView::setSparklineRange(int value) {
  settings.sparkline_range = value;
  updateToolBar();
  updateState();
}

void SignalView::handleSignalAdded(MessageId id, const cabana::Signal *sig) {
  if (id.address == model->msg_id.address) {
    selectSignal(sig);
  }
}

void SignalView::handleSignalUpdated(const cabana::Signal *sig) {
  if (int row = model->signalRow(sig); row != -1)
    updateState();
}

std::pair<QModelIndex, QModelIndex> SignalView::visibleSignalRange() {
  auto topLevelIndex = [](QModelIndex index) {
    while (index.isValid() && index.parent().isValid()) index = index.parent();
    return index;
  };

  const auto viewport_rect = tree->viewport()->rect();
  QModelIndex first_visible = tree->indexAt(viewport_rect.topLeft());
  if (first_visible.parent().isValid()) {
    first_visible = topLevelIndex(first_visible);
    first_visible = first_visible.siblingAtRow(first_visible.row() + 1);
  }

  QModelIndex last_visible = topLevelIndex(tree->indexAt(viewport_rect.bottomRight()));
  if (!last_visible.isValid()) {
    last_visible = model->index(model->rowCount() - 1, 0);
  }
  return {first_visible, last_visible};
}

void SignalView::updateState(const std::set<MessageId> *msgs) {
  const auto *last_msg = can->snapshot(model->msg_id);
  if (model->rowCount() == 0 || (msgs && !msgs->count(model->msg_id)) || last_msg->dat.size() == 0) return;

  for (auto item : model->root->children) {
    double value = 0;
    if (item->sig->getValue(last_msg->dat.data(), last_msg->dat.size(), &value)) {
      item->sig_val = item->sig->formatValue(value);
      max_value_width = std::max(max_value_width, fontMetrics().horizontalAdvance(item->sig_val));
    }
  }

  auto [first_visible, last_visible] = visibleSignalRange();
  if (first_visible.isValid() && last_visible.isValid()) {
    const static int min_max_width = QFontMetrics(delegate->minmax_font).horizontalAdvance("-000.00") + 5;
    int available_width = value_column_width - delegate->button_size.width();
    int value_width = std::min<int>(max_value_width + min_max_width, available_width / 2);
    QSize size(available_width - value_width,
               delegate->button_size.height() - style()->pixelMetric(QStyle::PM_FocusFrameVMargin) * 2);

    auto [first, last] = can->eventsInRange(model->msg_id, std::make_pair(last_msg->ts -settings.sparkline_range, last_msg->ts));
    QFutureSynchronizer<void> synchronizer;
    for (int i = first_visible.row(); i <= last_visible.row(); ++i) {
      auto item = model->getItem(model->index(i, 1));
      synchronizer.addFuture(QtConcurrent::run(
          &item->sparkline, &Sparkline::update, item->sig, first, last, settings.sparkline_range, size));
    }
    synchronizer.waitForFinished();
  }

  for (int i = 0; i < model->rowCount(); ++i) {
    emit model->dataChanged(model->index(i, 1), model->index(i, 1), {Qt::DisplayRole});
  }
}

void SignalView::resizeEvent(QResizeEvent* event) {
  updateState();
  QFrame::resizeEvent(event);
}

// ValueDescriptionDlg

ValueDescriptionDlg::ValueDescriptionDlg(const ValueDescription &descriptions, QWidget *parent) : QDialog(parent) {
  QHBoxLayout *toolbar_layout = new QHBoxLayout();
  QPushButton *add = new QPushButton(utils::icon("plus"), "");
  QPushButton *remove = new QPushButton(utils::icon("dash"), "");
  remove->setEnabled(false);
  toolbar_layout->addWidget(add);
  toolbar_layout->addWidget(remove);
  toolbar_layout->addStretch(0);

  table = new QTableWidget(descriptions.size(), 2, this);
  table->setItemDelegate(new Delegate(this));
  table->setHorizontalHeaderLabels({"Value", "Description"});
  table->horizontalHeader()->setStretchLastSection(true);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  int row = 0;
  for (auto &[val, desc] : descriptions) {
    table->setItem(row, 0, new QTableWidgetItem(QString::number(val)));
    table->setItem(row, 1, new QTableWidgetItem(desc));
    ++row;
  }

  auto btn_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->addLayout(toolbar_layout);
  main_layout->addWidget(table);
  main_layout->addWidget(btn_box);
  setMinimumWidth(500);

  connect(btn_box, &QDialogButtonBox::accepted, this, &ValueDescriptionDlg::save);
  connect(btn_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(add, &QPushButton::clicked, [this]() {
    table->setRowCount(table->rowCount() + 1);
    table->setItem(table->rowCount() - 1, 0, new QTableWidgetItem);
    table->setItem(table->rowCount() - 1, 1, new QTableWidgetItem);
  });
  connect(remove, &QPushButton::clicked, [this]() { table->removeRow(table->currentRow()); });
  connect(table, &QTableWidget::itemSelectionChanged, [=]() {
    remove->setEnabled(table->currentRow() != -1);
  });
}

void ValueDescriptionDlg::save() {
  for (int i = 0; i < table->rowCount(); ++i) {
    QString val = table->item(i, 0)->text().trimmed();
    QString desc = table->item(i, 1)->text().trimmed();
    if (!val.isEmpty() && !desc.isEmpty()) {
      val_desc.push_back({val.toDouble(), desc});
    }
  }
  QDialog::accept();
}

QWidget *ValueDescriptionDlg::Delegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
  QLineEdit *edit = new QLineEdit(parent);
  edit->setFrame(false);
  if (index.column() == 0) {
    edit->setValidator(new DoubleValidator(parent));
  }
  return edit;
}
