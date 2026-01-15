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

SignalEditor::SignalEditor(ChartsPanel *charts, QWidget *parent) : charts(charts), QFrame(parent) {
  setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  tree = new SignalTree(this);
  tree->setModel(model = new SignalTreeModel(this));
  tree->setItemDelegate(delegate = new SignalTreeDelegate(this));
  tree->setMinimumHeight(300);
  tree->header()->setSectionResizeMode(0, QHeaderView::Fixed);
  tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);

  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(0);
  main_layout->addWidget(createToolbar());
  main_layout->addWidget(tree);

  updateToolBar();
  setupConnections();

  setWhatsThis(tr(R"(
    <b>Signal view</b><br />
    <!-- TODO: add descprition here -->
  )"));
}

QWidget *SignalEditor::createToolbar() {
  QWidget* toolbar = new QWidget(this);
  QHBoxLayout* hl = new QHBoxLayout(toolbar);
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
  const int max_range = 30;  // 30s
  settings.sparkline_range = std::clamp(settings.sparkline_range, 1, max_range);
  hl->addWidget(sparkline_label = new QLabel());
  hl->addWidget(sparkline_range_slider = new QSlider(Qt::Horizontal, this));
  sparkline_range_slider->setRange(1, max_range);
  sparkline_range_slider->setValue(settings.sparkline_range);
  sparkline_range_slider->setToolTip(tr("Sparkline time range"));

  collapse_btn = new ToolButton("fold-vertical", tr("Collapse All"));
  collapse_btn->setIconSize({12, 12});
  hl->addWidget(collapse_btn);
  return toolbar;
}

void SignalEditor::setupConnections() {
  connect(filter_edit, &QLineEdit::textEdited, model, &SignalTreeModel::setFilter);
  connect(sparkline_range_slider, &QSlider::valueChanged, this, &SignalEditor::setSparklineRange);
  connect(collapse_btn, &QPushButton::clicked, tree, &QTreeView::collapseAll);
  connect(tree, &SignalTree::highlightRequested, this, &SignalEditor::highlight);
  connect(model, &QAbstractItemModel::modelReset, this, &SignalEditor::rowsChanged);
  connect(model, &QAbstractItemModel::rowsRemoved, this, &SignalEditor::rowsChanged);
  connect(model, &QAbstractItemModel::rowsInserted, this, &SignalEditor::rowsChanged);
  connect(GetDBC(), &dbc::Manager::signalAdded, this, &SignalEditor::handleSignalAdded);
  connect(GetDBC(), &dbc::Manager::signalUpdated, this, &SignalEditor::handleSignalUpdated);
  connect(tree->verticalScrollBar(), &QScrollBar::valueChanged, [this]() { updateState(); });
  connect(tree->verticalScrollBar(), &QScrollBar::rangeChanged, [this]() { updateState(); });
  connect(&StreamManager::instance(), &StreamManager::snapshotsUpdated, this, &SignalEditor::updateState);
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

void SignalEditor::updateChartState() {
  if (model && model->rowCount() > 0) {
    // This triggers a repaint of the Value column (1) for all rows
    emit model->dataChanged(model->index(0, 1),
                            model->index(model->rowCount() - 1, 1),
                            {Qt::DisplayRole});

    // Also ensure the viewport physically refreshes
    tree->viewport()->update();
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

void SignalEditor::updateState(const std::set<MessageId> *msgs) {
  const auto *last_msg = StreamManager::stream()->snapshot(model->msg_id);
  if (model->rowCount() == 0 || (msgs && !msgs->count(model->msg_id)) || last_msg->dat.size() == 0) return;

  auto [first_visible, last_visible] = visibleSignalRange();
  if (!first_visible.isValid() || !last_visible.isValid()) return;

  const int btn_width = delegate->getButtonsWidth();
  const int value_width = delegate->kValueWidth;
  const int spark_w = std::max(10, value_column_width - btn_width - value_width - (delegate->kPadding * 2));
  const QSize spark_size(spark_w, delegate->kBtnSize);

  // Prepare data window for sparklines
  auto [first, last] = StreamManager::stream()->eventsInRange(
      model->msg_id,
      std::make_pair(last_msg->ts - settings.sparkline_range, last_msg->ts));

  // Update items in the visible range
  QFutureSynchronizer<void> synchronizer;
  for (int i = first_visible.row(); i <= last_visible.row(); ++i) {
    auto item = model->getItem(model->index(i, 1));
    double value = 0;
    if (item->sig->getValue(last_msg->dat.data(), last_msg->dat.size(), &value)) {
      item->sig_val = item->sig->formatValue(value);
    }
    synchronizer.addFuture(QtConcurrent::run(
        &item->sparkline, &Sparkline::update, item->sig, first, last, settings.sparkline_range, spark_size));
  }
  synchronizer.waitForFinished();

  emit model->dataChanged(model->index(first_visible.row(), 1), model->index(last_visible.row(), 1), {Qt::DisplayRole});
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

  tree->setColumnWidth(0, finalWidth);
  value_column_width = tree->viewport()->width() - finalWidth;

  updateState();
}

void SignalEditor::resizeEvent(QResizeEvent* event) {
  QFrame::resizeEvent(event);
  updateColumnWidths();
}
