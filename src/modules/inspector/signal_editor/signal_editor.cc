#include "signal_editor.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QtConcurrent>
#include <QVBoxLayout>

#include "core/commands/commands.h"
#include "modules/settings/settings.h"
#include "modules/system/stream_manager.h"

SignalEditor::SignalEditor(ChartsPanel *charts, QWidget *parent) : QFrame(parent) {
  setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  tree = new SignalTree(this);
  tree->setModel(model = new SignalTreeModel(this));
  tree->setItemDelegate(delegate = new SignalTreeDelegate(this));
  tree->setMinimumHeight(300);

  QHeaderView* header = tree->header();
  header->setCascadingSectionResizes(false);
  header->setStretchLastSection(true);
  header->setSectionResizeMode(0, QHeaderView::Interactive);
  header->setSectionResizeMode(1, QHeaderView::Stretch);

  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(0);
  main_layout->addWidget(createToolbar());
  main_layout->addWidget(tree);

  updateToolBar();
  setupConnections(charts);

  setWhatsThis(tr(R"(
    <b>Signal view</b><br />
    <!-- TODO: add descprition here -->
  )"));
}

QWidget *SignalEditor::createToolbar() {
  QWidget* toolbar = new QWidget(this);
  QHBoxLayout* hl = new QHBoxLayout(toolbar);
  hl->setContentsMargins(4, 4, 4, 4);

  hl->addWidget(signal_count_lb = new QLabel());
  filter_edit = new QLineEdit(this);

  QRegularExpression re("\\S+");
  filter_edit->setValidator(new QRegularExpressionValidator(re, this));
  filter_edit->setClearButtonEnabled(true);
  filter_edit->setPlaceholderText(tr("Filter Signal"));
  hl->addWidget(filter_edit, 0, Qt::AlignCenter);
  hl->addStretch(1);

  // WARNING: increasing the maximum range can result in severe performance degradation.
  // 30s is a reasonable value at present.
  const int max_range = 30;  // 30s
  settings.sparkline_range = std::clamp(settings.sparkline_range, 1, max_range);
  hl->addWidget(sparkline_label = new QLabel());
  hl->addWidget(sparkline_range_slider = new QSlider(Qt::Horizontal, this));
  sparkline_range_slider->setMinimumWidth(100);
  sparkline_range_slider->setRange(1, max_range);
  sparkline_range_slider->setValue(settings.sparkline_range);
  sparkline_range_slider->setToolTip(tr("Sparkline time range"));

  collapse_btn = new ToolButton("fold-vertical", tr("Collapse All"));
  collapse_btn->setIconSize({12, 12});
  hl->addWidget(collapse_btn);

  return toolbar;
}

void SignalEditor::setupConnections(ChartsPanel *charts) {
  connect(filter_edit, &QLineEdit::textEdited, model, &SignalTreeModel::setFilter);
  connect(sparkline_range_slider, &QSlider::valueChanged, this, &SignalEditor::setSparklineRange);
  connect(collapse_btn, &QPushButton::clicked, tree, &QTreeView::collapseAll);
  connect(model, &QAbstractItemModel::modelReset, this, &SignalEditor::rowsChanged);
  connect(model, &QAbstractItemModel::rowsRemoved, this, &SignalEditor::rowsChanged);
  connect(model, &QAbstractItemModel::rowsInserted, this, &SignalEditor::rowsChanged);
  connect(GetDBC(), &dbc::Manager::signalAdded, this, &SignalEditor::handleSignalAdded);
  connect(GetDBC(), &dbc::Manager::signalUpdated, this, &SignalEditor::handleSignalUpdated);
  connect(tree, &SignalTree::highlightRequested, this, &SignalEditor::highlight);
  connect(tree->verticalScrollBar(), &QScrollBar::valueChanged, [this]() { updateState(); });
  connect(tree->verticalScrollBar(), &QScrollBar::rangeChanged, [this]() { updateState(); });
  connect(&StreamManager::instance(), &StreamManager::snapshotsUpdated, this, &SignalEditor::updateState);
  connect(charts, &ChartsPanel::seriesChanged, model, [this, charts]() {
    model->updateChartedSignals(charts->getChartedSignals());
  });

  connect(delegate, &SignalTreeDelegate::removeRequested, this, [this](const QModelIndex& index) {
    auto item = (SignalTreeModel::Item*)index.internalPointer();
    UndoStack::push(new RemoveSigCommand(model->msg_id, item->sig));
  });
  connect(delegate, &SignalTreeDelegate::plotRequested, this, [this](const QModelIndex& index, bool merge) {
    auto item = (SignalTreeModel::Item*)index.internalPointer();
    emit showChart(model->msg_id, item->sig, !index.data(IsChartedRole).toBool(), merge);
  });
}

void SignalEditor::setMessage(const MessageId &id) {
  filter_edit->clear();
  model->setMessage(id);
}

void SignalEditor::clearMessage() {
  filter_edit->clear();
  model->setMessage(MessageId());
}

void SignalEditor::rowsChanged() {
  updateToolBar();
  updateColumnWidths();
  delegate->value_width = 0;
}

void SignalEditor::selectSignal(const dbc::Signal *sig, bool expand) {
  if (int row = model->signalRow(sig); row != -1) {
    auto idx = model->index(row, 0);
    if (expand) {
      tree->setExpanded(idx, !tree->isExpanded(idx));
    }
    tree->scrollTo(idx, QAbstractItemView::PositionAtTop);
    tree->setCurrentIndex(idx);
  }
}

void SignalEditor::signalHovered(const dbc::Signal *sig) {
  auto &children = model->root->children;
  for (int i = 0; i < children.size(); ++i) {
    bool highlight = children[i]->sig == sig;
    if (std::exchange(children[i]->highlight, highlight) != highlight) {
      emit model->dataChanged(model->index(i, 0), model->index(i, 0), {Qt::DecorationRole});
      emit model->dataChanged(model->index(i, 1), model->index(i, 1), {Qt::DisplayRole});
    }
  }
}

void SignalEditor::updateToolBar() {
  signal_count_lb->setText(tr("Signals: %1").arg(model->rowCount()));
  sparkline_label->setText(utils::formatSeconds(settings.sparkline_range));
}

void SignalEditor::setSparklineRange(int value) {
  settings.sparkline_range = value;
  updateToolBar();
  // Clear history to prevent scaling artifacts when range changes drastically
  for (auto item : model->root->children) {
    item->sparkline.clearHistory();
  }
  updateState();
}

void SignalEditor::handleSignalAdded(MessageId id, const dbc::Signal *sig) {
  if (id.address == model->msg_id.address) {
    selectSignal(sig);
  }
}

void SignalEditor::handleSignalUpdated(const dbc::Signal *sig) {
  if (int row = model->signalRow(sig); row != -1)
    updateState();
}

std::pair<QModelIndex, QModelIndex> SignalEditor::visibleSignalRange() {
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

void SignalEditor::updateState(const std::set<MessageId>* msgs) {
  const auto* last_msg = StreamManager::stream()->snapshot(model->msg_id);
  if (model->rowCount() == 0 || (msgs && !msgs->count(model->msg_id)) || last_msg->dat.size() == 0) return;

  auto [first_v, last_v] = visibleSignalRange();
  if (!first_v.isValid()) return;


  int value_w = getValueColumnWidth(last_msg);
  if (value_w > delegate->value_width) {
    delegate->value_width = value_w;
  } else if (delegate->value_width - value_w > 25) {
    // prevent jerky resizing by only decreasing width if difference is significant
    delegate->value_width = value_w;
  }

  int fixed_parts = delegate->getButtonsWidth() + delegate->value_width +
                    (delegate->kPadding * 4);
  int spark_w = std::max(10, value_column_width - fixed_parts);
  const QSize spark_sz(spark_w, delegate->kBtnSize);
  auto range = StreamManager::stream()->eventsInRange(
      model->msg_id, std::make_pair(last_msg->ts - settings.sparkline_range, last_msg->ts));


  QVector<SignalTreeModel::Item*> items;
  for (int i = first_v.row(); i <= last_v.row(); ++i) {
    items << model->getItem(model->index(i, 1));
  }

  QtConcurrent::blockingMap(items, [&](SignalTreeModel::Item* item) {
    item->sparkline.update(item->sig, range.first, range.second, settings.sparkline_range, spark_sz);
  });

  emit model->dataChanged(model->index(first_v.row(), 1), model->index(last_v.row(), 1), {Qt::DisplayRole});
}

int SignalEditor::getValueColumnWidth(const MessageState* msg) {
  static int digit_w = QFontMetrics(delegate->value_font).horizontalAdvance('0');
  static int minmax_digit_w = QFontMetrics(delegate->minmax_font).horizontalAdvance('0');

  int global_minmax_w = minmax_digit_w * 4;  // initial width for Min/Max
  int global_value_w = 0;
  for (int i = 0; i < model->rowCount(); ++i) {
    auto* item = model->getItem(model->index(i, 1));
    double val = 0;
    if (item->sig->getValue(msg->dat.data(), msg->dat.size(), &val)) {
      item->sig_val = item->sig->formatValue(val);
      global_value_w = std::max(global_value_w, (item->sig_val.size() * digit_w));
    }
  }

  return std::clamp(global_value_w + global_minmax_w + delegate->kPadding, 50, value_column_width / 3);
}

void SignalEditor::updateColumnWidths() {
  auto* m = GetDBC()->msg(model->msg_id);
  if (!m) return;

  int max_content_w = 0;
  int indentation = tree->indentation();

  for (const auto *sig : m->getSignals()) {
    int w = delegate->nameColumnWidth(sig);
    if (sig->type == dbc::Signal::Type::Multiplexed) {
      w += indentation;
    }

    max_content_w = std::max(max_content_w, w);
  }

  int maxAllowedWidth = std::max(150, this->width() / 3);
  int finalWidth = std::clamp(max_content_w, 150, maxAllowedWidth);

  // Block signals to prevent resizeEvent recursion
  tree->header()->blockSignals(true);
  tree->setColumnWidth(0, finalWidth);
  tree->header()->blockSignals(false);

  value_column_width = tree->viewport()->width() - finalWidth;

  updateState();
}

void SignalEditor::resizeEvent(QResizeEvent* event) {
  QFrame::resizeEvent(event);
  updateColumnWidths();
}
