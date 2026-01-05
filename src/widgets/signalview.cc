#include "signalview.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QtConcurrent>
#include <QVBoxLayout>

#include "commands.h"
#include "settings.h"

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
  tree->setItemDelegate(delegate = new SignalTreeDelegate(this));
  tree->setFrameShape(QFrame::NoFrame);
  tree->setHeaderHidden(true);
  tree->setMouseTracking(true);
  tree->setExpandsOnDoubleClick(false);
  tree->setEditTriggers(QAbstractItemView::AllEditTriggers);
  tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  tree->header()->setStretchLastSection(true);
  tree->setMinimumHeight(300);
  // tree->viewport()->setMouseTracking(true);
  tree->viewport()->setAttribute(Qt::WA_AlwaysShowToolTips, true);
  tree->setToolTipDuration(1000);

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
  updateToolBar();
  updateState();
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
  if (model && model->rowCount() > 0) {
    // This triggers a repaint of the Value column (1) for all rows
    emit model->dataChanged(model->index(0, 1),
                            model->index(model->rowCount() - 1, 1),
                            {Qt::DisplayRole});

    // Also ensure the viewport physically refreshes
    tree->viewport()->update();
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
    const int btn_column_width = delegate->getButtonsWidth();
    int available_width = value_column_width - btn_column_width;
    int value_width = std::min<int>(max_value_width + min_max_width, available_width / 2);
    int h = fontMetrics().height();
    QSize size(std::max(10, available_width - value_width), h);
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
