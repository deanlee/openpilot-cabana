#include "signal_editor.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

#include "core/commands/commands.h"
#include "modules/settings/settings.h"

SignalEditor::SignalEditor(ChartsPanel* charts, QWidget* parent) : QFrame(parent) {
  setFrameStyle(QFrame::NoFrame);
  tree_ = new SignalTree(this);
  tree_->setModel(model = new SignalTreeModel(this));
  tree_->setItemDelegate(delegate_ = new SignalTreeDelegate(this));
  tree_->setMinimumHeight(300);

  QHeaderView* header = tree_->header();
  header->setCascadingSectionResizes(false);
  header->setStretchLastSection(true);
  header->setSectionResizeMode(0, QHeaderView::Interactive);
  header->setSectionResizeMode(1, QHeaderView::Stretch);

  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);
  mainLayout->addWidget(createToolbar());

  QFrame* line = new QFrame(this);
  line->setFrameStyle(QFrame::HLine | QFrame::Sunken);
  mainLayout->addWidget(line);

  mainLayout->addWidget(tree_);

  updateToolBar();
  setupConnections(charts);

  setWhatsThis(tr(R"(
    <b>Signal view</b><br />
    <!-- TODO: add description here -->
  )"));
}

QWidget* SignalEditor::createToolbar() {
  QWidget* toolbar = new QWidget(this);
  QHBoxLayout* layout = new QHBoxLayout(toolbar);
  layout->setContentsMargins(4, 4, 4, 4);

  layout->addWidget(signalCountLabel_ = new QLabel());

  filterEdit_ = new DebouncedLineEdit(this);
  QRegularExpression re("\\S+");
  filterEdit_->setValidator(new QRegularExpressionValidator(re, this));
  filterEdit_->setClearButtonEnabled(true);
  filterEdit_->setPlaceholderText(tr("Filter signal..."));
  filterEdit_->setToolTip(tr("Filter signals by name"));
  layout->addWidget(filterEdit_, 0, Qt::AlignCenter);
  layout->addStretch(1);

  // Sparkline range slider (max 30s to avoid performance issues)
  const int maxRange = 30;
  settings.sparkline_range = std::clamp(settings.sparkline_range, 1, maxRange);
  layout->addWidget(sparklineLabel_ = new QLabel());
  layout->addWidget(sparklineRangeSlider_ = new QSlider(Qt::Horizontal, this));
  sparklineRangeSlider_->setMinimumWidth(100);
  sparklineRangeSlider_->setRange(1, maxRange);
  sparklineRangeSlider_->setValue(settings.sparkline_range);
  sparklineRangeSlider_->setToolTip(tr("Adjust sparkline history duration"));

  collapseBtn_ = new ToolButton("fold-vertical", tr("Collapse All"));
  layout->addWidget(collapseBtn_);

  return toolbar;
}

void SignalEditor::setupConnections(ChartsPanel* charts) {
  connect(filterEdit_, &DebouncedLineEdit::debouncedTextEdited, model, &SignalTreeModel::setFilter);
  connect(sparklineRangeSlider_, &QSlider::valueChanged, this, &SignalEditor::setSparklineRange);
  connect(collapseBtn_, &QPushButton::clicked, tree_, &QTreeView::collapseAll);

  connect(model, &QAbstractItemModel::modelReset, this, &SignalEditor::rowsChanged);
  connect(model, &QAbstractItemModel::rowsRemoved, this, &SignalEditor::rowsChanged);
  connect(model, &QAbstractItemModel::rowsInserted, this, &SignalEditor::rowsChanged);
  connect(model, &QAbstractItemModel::modelAboutToBeReset, delegate_, &SignalTreeDelegate::clearHoverState);
  connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, delegate_, &SignalTreeDelegate::clearHoverState);

  connect(GetDBC(), &dbc::Manager::signalAdded, this, &SignalEditor::handleSignalAdded);
  connect(GetDBC(), &dbc::Manager::signalUpdated, this, &SignalEditor::handleSignalUpdated);

  connect(tree_, &SignalTree::highlightRequested, this, &SignalEditor::highlight);
  connect(tree_->verticalScrollBar(), &QScrollBar::valueChanged, [this]() { updateState(); });
  connect(tree_->verticalScrollBar(), &QScrollBar::rangeChanged, [this]() { updateState(); });

  connect(charts, &ChartsPanel::seriesChanged, model,
          [this, charts]() { model->updateChartedSignals(charts->getChartedSignals()); });

  connect(delegate_, &SignalTreeDelegate::removeRequested, this,
          [this](const dbc::Signal* sig) { UndoStack::push(new RemoveSigCommand(model->messageId(), sig)); });

  connect(delegate_, &SignalTreeDelegate::plotRequested, this, [this](const dbc::Signal* sig, bool show, bool merge) {
    emit showChart(model->messageId(), sig, show, merge);
  });
}

void SignalEditor::setMessage(const MessageId& id) {
  filterEdit_->clear();
  model->setMessage(id);
  tree_->scrollToTop();
}

void SignalEditor::clearMessage() {
  filterEdit_->clear();
  model->setMessage(MessageId());
  tree_->scrollToTop();
}

void SignalEditor::updateState(const std::set<MessageId>* msgs) {
  // Skip update if widget is hidden or collapsed
  if (!isVisible() || height() == 0 || width() == 0) return;

  const auto* lastMsg = StreamManager::stream()->snapshot(model->messageId());
  if (model->rowCount() == 0 || (msgs && !msgs->count(model->messageId()))) return;

  auto [firstV, lastV] = visibleSignalRange();
  if (!firstV.isValid()) return;

  model->updateValues(lastMsg);

  int fixedParts = delegate_->getButtonsWidth() + model->maxValueWidth() + (SignalTreeDelegate::kPadding * 4);
  int valueColWidth = tree_->columnWidth(1);
  int sparkW = std::max(valueColWidth - fixedParts, valueColWidth / 2);
  model->updateSparklines(lastMsg, firstV.row(), lastV.row(), QSize(sparkW, SignalTreeDelegate::kBtnSize));
}

void SignalEditor::rowsChanged() {
  updateToolBar();
  updateColumnWidths();
}

void SignalEditor::selectSignal(const dbc::Signal* sig, bool expand) {
  int row = model->signalRow(sig);
  if (row == -1) return;

  auto idx = model->index(row, 0);
  if (expand) {
    tree_->setExpanded(idx, !tree_->isExpanded(idx));
  }
  tree_->scrollTo(idx, QAbstractItemView::PositionAtTop);
  tree_->setCurrentIndex(idx);
}

void SignalEditor::updateToolBar() {
  signalCountLabel_->setText(tr("Signals: %1").arg(model->rowCount()));
  sparklineLabel_->setText(utils::formatSeconds(settings.sparkline_range));
}

void SignalEditor::setSparklineRange(int value) {
  settings.sparkline_range = value;
  updateToolBar();

  // Clear history to prevent scaling artifacts
  model->resetSparklines();
  updateState();
}

void SignalEditor::handleSignalAdded(MessageId id, const dbc::Signal* sig) {
  if (id.address == model->messageId().address) {
    selectSignal(sig);
  }
}

void SignalEditor::handleSignalUpdated(const dbc::Signal* sig) {
  if (model->signalRow(sig) != -1) {
    updateState();
  }
}

std::pair<QModelIndex, QModelIndex> SignalEditor::visibleSignalRange() {
  auto topLevelIndex = [](QModelIndex index) {
    while (index.isValid() && index.parent().isValid()) {
      index = index.parent();
    }
    return index;
  };

  const auto viewportRect = tree_->viewport()->rect();
  QModelIndex firstVisible = tree_->indexAt(viewportRect.topLeft());

  if (firstVisible.parent().isValid()) {
    firstVisible = topLevelIndex(firstVisible);
    firstVisible = firstVisible.siblingAtRow(firstVisible.row() + 1);
  }

  QModelIndex lastVisible = topLevelIndex(tree_->indexAt(viewportRect.bottomRight()));
  if (!lastVisible.isValid()) {
    lastVisible = model->index(model->rowCount() - 1, 0);
  }

  return {firstVisible, lastVisible};
}

void SignalEditor::updateColumnWidths() {
  auto* msg = GetDBC()->msg(model->messageId());
  if (!msg) return;

  const int limit = std::max(150, tree_->viewport()->width() / 3);
  int maxContentW = 0;
  int indentation = tree_->indentation();

  for (const auto* sig : msg->getSignals()) {
    int w = delegate_->nameColumnWidth(sig) + indentation;
    maxContentW = std::max(maxContentW, w);
    if (maxContentW > limit) break;
  }

  int finalWidth = std::clamp(maxContentW, 150, limit);

  // Block signals to prevent resizeEvent recursion
  tree_->header()->blockSignals(true);
  tree_->setColumnWidth(0, finalWidth);
  tree_->header()->blockSignals(false);

  updateState();
}

void SignalEditor::resizeEvent(QResizeEvent* event) {
  QFrame::resizeEvent(event);
  if (event->oldSize().width() != event->size().width()) {
    updateColumnWidths();
  }
}
