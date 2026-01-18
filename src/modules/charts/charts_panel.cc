#include "charts_panel.h"

#include <QApplication>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QtConcurrent>

#include "chart_view.h"
#include "components/charts_container.h"
#include "modules/settings/settings.h"
#include "modules/system/stream_manager.h"

ChartsPanel::ChartsPanel(QWidget *parent) : QFrame(parent) {
  setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  auto *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(0);

  toolbar = new ChartsToolBar(this);
  tab_manager_ = new ChartsTabManager(this);

  stack_ = new QStackedWidget(this);
  scroll_area_ = new ChartsScrollArea(this);
  empty_view_ = new ChartsEmptyView(this);
  stack_->addWidget(empty_view_);
  stack_->addWidget(scroll_area_);

  main_layout->addWidget(toolbar);
  main_layout->addWidget(tab_manager_->tabbar_);
  main_layout->addWidget(stack_);

  // init settings
  current_theme = settings.theme;
  column_count = std::clamp(settings.chart_column_count, 1, MAX_COLUMN_COUNT);
  max_chart_range = std::clamp(settings.chart_range, 1, settings.max_cached_minutes * 60);
  auto min_sec = StreamManager::stream()->minSeconds();
  display_range = std::make_pair(min_sec, min_sec + max_chart_range);

  align_timer = new QTimer(this);
  align_timer->setSingleShot(true);
  setupConnections();

  setWhatsThis(tr(R"(
    <b>Chart View</b><br />
    <b>Click</b>: Click to seek to a corresponding time.<br />
    <b>Drag</b>: Zoom into the chart.<br />
    <b>Shift + Drag</b>: Scrub through the chart to view values.<br />
    <b>Right Mouse</b>: Open the context menu.<br />
  )"));
}

void ChartsPanel::setupConnections() {
  connect(align_timer, &QTimer::timeout, this, &ChartsPanel::alignCharts);
  connect(GetDBC(), &dbc::Manager::DBCFileChanged, this, &ChartsPanel::removeAll);
  connect(&StreamManager::instance(), &StreamManager::eventsMerged, this, &ChartsPanel::eventsMerged);
  connect(&StreamManager::instance(), &StreamManager::snapshotsUpdated, this, &ChartsPanel::updateState);
  connect(&StreamManager::instance(), &StreamManager::seeking, this, &ChartsPanel::updateState);
  connect(&StreamManager::instance(), &StreamManager::timeRangeChanged, this, &ChartsPanel::timeRangeChanged);
  connect(toolbar, &ChartsToolBar::rangeChanged, this, &ChartsPanel::setMaxChartRange);
  connect(toolbar->new_plot_btn, &QToolButton::clicked, this, &ChartsPanel::newChart);
  connect(toolbar->remove_all_btn, &QToolButton::clicked, this, &ChartsPanel::removeAll);
  connect(toolbar->new_tab_btn, &QToolButton::clicked, tab_manager_, &ChartsTabManager::addTab);
  connect(toolbar->dock_btn, &QToolButton::clicked, this, &ChartsPanel::toggleChartsDocking);
  connect(toolbar, &ChartsToolBar::columnCountChanged, this, &ChartsPanel::setColumnCount);
  connect(toolbar, &ChartsToolBar::seriesTypeChanged, this, &ChartsPanel::settingChanged);
  connect(&settings, &Settings::changed, this, &ChartsPanel::settingChanged);

  connect(this, &ChartsPanel::seriesChanged, tab_manager_, &ChartsTabManager::updateLabels);
  connect(tab_manager_, &ChartsTabManager::tabAboutToBeRemoved, this, &ChartsPanel::removeCharts);
  connect(tab_manager_, &ChartsTabManager::currentTabChanged, this, [this]() { updateLayout(true); });

  connect(scroll_area_->container_, &ChartsContainer::chartDropped, this, &ChartsPanel::handleChartDrop);
  connect(scroll_area_->verticalScrollBar(), &QScrollBar::valueChanged, this, &ChartsPanel::updateHoverFromCursor);
}

void ChartsPanel::eventsMerged(const MessageEventsMap& new_events) {
  if (charts.empty()) return;

  QtConcurrent::blockingMap(charts, [&new_events](ChartView* c) {
    if (c && c->chart_) {
      c->chart_->prepareData(nullptr, &new_events);
    }
  });

  for (auto* c : charts) {
    c->chart_->updateSeries(nullptr);
  }
}

void ChartsPanel::timeRangeChanged(const std::optional<std::pair<double, double>> &time_range) {
  toolbar->updateState(charts.size());
  updateState();
}

void ChartsPanel::updateHover(double time) {
  emit showTip(time);
  hover_time_ = time;

  int scroll_y = scroll_area_->verticalScrollBar()->value();
  int view_h = scroll_area_->viewport()->height();
  QRect visible_window(0, scroll_y, scroll_area_->widget()->width(), view_h);

  for (auto* chart_view : charts) {
    QPoint chart_pos = chart_view->mapTo(scroll_area_->widget(), QPoint(0, 0));
    QRect plot_rect = chart_view->chart_->plotArea().toRect();
    plot_rect.moveTopLeft(chart_pos + plot_rect.topLeft());

    QRect intersected = plot_rect.intersected(visible_window);

    if (hover_time_ >= 0 && !intersected.isEmpty()) {
      intersected.moveTopLeft(intersected.topLeft() - chart_pos);
      chart_view->showTip(hover_time_, intersected);
    } else {
      chart_view->hideTip();
    }
  }
}

void ChartsPanel::hideHover() {
  hover_time_ = -1;
  for (auto* chart_view : charts) {
    chart_view->hideTip();
  }
}

void ChartsPanel::updateHoverFromCursor() {
  if (hover_time_ < 0) return;

  QPoint global_mouse_pos = QCursor::pos();
  for (auto* c : charts) {
    if (c->rect().contains(c->mapFromGlobal(global_mouse_pos))) {
      hover_time_ = c->secondsAtPoint(c->mapFromGlobal(global_mouse_pos));
      updateHover(hover_time_);
      return;
    }
  }
  hideHover();
}

void ChartsPanel::updateState() {
  bool has_charts = !charts.isEmpty();
  stack_->setCurrentIndex(has_charts ? 1 : 0);

  if (!has_charts) return;

  auto *stream = StreamManager::stream();
  const double cur_sec = stream->currentSec();
  const double prev_display_start = display_range.first;
  const double prev_display_end = display_range.second;

  const auto &manual_range = stream->timeRange();
  if (!manual_range) {
    // 1. Shift the window if the playhead leaves the 0-80% "viewing zone"
    double pos = (cur_sec - display_range.first) / max_chart_range;
    if (pos < 0 || pos > 0.8) {
      display_range.first = std::max(stream->minSeconds(), cur_sec - max_chart_range * 0.1);
    }

    // 2. Clamp the window to the absolute stream boundaries
    double start = std::clamp(display_range.first, stream->minSeconds(), std::max(stream->minSeconds(), stream->maxSeconds() - max_chart_range));
    display_range = {start, start + max_chart_range};
  }

  const auto &range = manual_range.value_or(display_range);
  for (auto c : charts) {
    c->updatePlot(cur_sec, range.first, range.second);
  }

  if (hover_time_ >= 0 && std::abs(prev_display_start - display_range.first) > EPSILON ||
      std::abs(prev_display_end - display_range.second) > EPSILON) {
    updateHoverFromCursor();
  }
}

void ChartsPanel::setMaxChartRange(int value) {
  max_chart_range = value;
  updateState();
}

void ChartsPanel::settingChanged() {
  if (std::exchange(current_theme, settings.theme) != current_theme) {
    auto theme = utils::isDarkTheme() ? QChart::QChart::ChartThemeDark : QChart::ChartThemeLight;
    for (auto c : charts) {
      c->chart_->setTheme(theme);
    }
  }
  for (auto c : charts) {
    c->setFixedHeight(settings.chart_height);
    c->chart_->setSeriesType((SeriesType)settings.chart_series_type);
    c->resetChartCache();
  }
}

ChartView *ChartsPanel::findChart(const MessageId &id, const dbc::Signal *sig) {
  for (auto c : charts)
    if (c->chart_->hasSignal(id, sig)) return c;
  return nullptr;
}

const QSet<const dbc::Signal*> ChartsPanel::getChartedSignals() const {
  QSet<const dbc::Signal*> charted_signals;
  for (auto* c : charts) {
    for (const auto& s : c->chart_->sigs_) {
      charted_signals.insert(const_cast<const dbc::Signal*>(s.sig));
    }
  }
  return charted_signals;
}

ChartView *ChartsPanel::createChart(int pos) {
  auto chart = new ChartView(StreamManager::stream()->timeRange().value_or(display_range), this);
  chart->viewport()->installEventFilter(scroll_area_->container_);
  connect(chart, &ChartView::axisYLabelWidthChanged, align_timer, qOverload<>(&QTimer::start));
  pos = std::clamp(pos, 0, charts.size());
  charts.insert(pos, chart);
  tab_manager_->addChartToCurrentTab(chart);
  updateLayout(true);
  toolbar->updateState(charts.size());
  return chart;
}

void ChartsPanel::showChart(const MessageId &id, const dbc::Signal *sig, bool show, bool merge) {
  ChartView *c = findChart(id, sig);
  if (show && !c) {
    c = merge && tab_manager_->currentCharts().size() > 0 ? tab_manager_->currentCharts().front() : createChart();
    c->chart_->addSignal(id, sig);
    updateState();
  } else if (!show && c) {
    c->chart_->removeIf([&](auto &s) { return s.msg_id == id && s.sig == sig; });
  }
}

void ChartsPanel::mergeCharts(ChartView* chart, ChartView* target) {
  target->setUpdatesEnabled(false);

  target->chart_->takeSignals(std::move(chart->chart_->sigs_));
  chart->chart_->sigs_.clear();
  removeChart(chart);

  target->setUpdatesEnabled(true);
  target->startAnimation();
}

void ChartsPanel::moveChart(ChartView* chart, ChartView* target, DropMode mode) {
  tab_manager_->removeChart(chart);

  auto& current_charts = tab_manager_->currentCharts();

  int target_idx = current_charts.indexOf(target);
  if (target_idx == -1) return;  // Safety check

  int insert_at = (mode == DropMode::InsertAfter) ? target_idx + 1 : target_idx;
  current_charts.insert(std::clamp(insert_at, 0, current_charts.size()), chart);

  updateLayout(true);
  chart->startAnimation();
}

void ChartsPanel::splitChart(ChartView* src_view) {
  auto& src_sigs = src_view->chart_->sigs_;
  if (src_sigs.size() <= 1) return;

  // 1. Transaction safety: disable updates
  src_view->setUpdatesEnabled(false);

  int target_pos = charts.indexOf(src_view) + 1;

  // 2. Logic: Create one new chart for EVERY extra signal
  while (src_sigs.size() > 1) {
    ChartView* dest_view = createChart(target_pos++);

    std::vector<ChartSignal> to_move;
    to_move.push_back(std::move(src_sigs.back()));
    src_sigs.pop_back();

    dest_view->chart_->takeSignals(std::move(to_move));
  }

  // 3. Finalize source chart
  src_view->chart_->syncUI();
  src_view->setUpdatesEnabled(true);
}

QStringList ChartsPanel::serializeChartIds() const {
  QStringList chart_ids;
  for (auto c : charts) {
    QStringList ids;
    for (const auto& s : c->chart_->sigs_)
      ids += QString("%1|%2").arg(s.msg_id.toString(), s.sig->name);
    chart_ids += ids.join(',');
  }
  std::reverse(chart_ids.begin(), chart_ids.end());
  return chart_ids;
}

void ChartsPanel::restoreChartsFromIds(const QStringList& chart_ids) {
  for (const auto& chart_id : chart_ids) {
    int index = 0;
    for (const auto& part : chart_id.split(',')) {
      const auto sig_parts = part.split('|');
      if (sig_parts.size() != 2) continue;
      MessageId msg_id = MessageId::fromString(sig_parts[0]);
      if (auto* msg = GetDBC()->msg(msg_id))
        if (auto* sig = msg->sig(sig_parts[1]))
          showChart(msg_id, sig, true, index++ > 0);
    }
  }
}

void ChartsPanel::setColumnCount(int n) {
  n = std::clamp(n, 1, MAX_COLUMN_COUNT);
  if (column_count != n) {
    column_count = n;
    updateLayout();
  }
}

void ChartsPanel::updateLayout(bool force) {
  scroll_area_->container_->updateLayout(tab_manager_->currentCharts(), column_count, force);
}

QSize ChartsPanel::minimumSizeHint() const {
  return QSize(CHART_MIN_WIDTH * 1.5 * qApp->devicePixelRatio(), QWidget::minimumSizeHint().height());
}

void ChartsPanel::newChart() {
  SignalPicker dlg(tr("New Chart"), this);
  if (dlg.exec() == QDialog::Accepted) {
    auto items = dlg.seletedItems();
    if (!items.isEmpty()) {
      auto c = createChart();
      for (auto it : items) {
        c->chart_->addSignal(it->msg_id, it->sig);
      }
    }
  }
}

void ChartsPanel::removeCharts(QList<ChartView*> charts_to_remove) {
  for (auto chart : charts_to_remove) {
    charts.removeOne(chart);
    chart->deleteLater();
  }
  toolbar->updateState(charts.size());
  updateLayout(true);
  alignCharts();
  emit seriesChanged();
}

void ChartsPanel::removeChart(ChartView *chart) {
  charts.removeOne(chart);
  chart->deleteLater();
  tab_manager_->removeChart(chart);
  toolbar->updateState(charts.size());
  updateLayout(true);
  alignCharts();
  emit seriesChanged();
}

void ChartsPanel::removeAll() {
  if (!charts.isEmpty()) {
    for (auto c : charts) {
      c->deleteLater();
    }
    charts.clear();
    emit seriesChanged();
  }

  tab_manager_->clear();
  toolbar->zoomReset();
  toolbar->updateState(0);
  updateLayout(true);
}

void ChartsPanel::alignCharts() {
  int plot_left = 0;
  for (auto c : charts) {
    plot_left = std::max(plot_left, c->chart_->y_label_width_);
  }
  plot_left = std::max((plot_left / 10) * 10 + 10, 50);
  for (auto c : charts) {
    c->chart_->alignLayout(plot_left);
  }
}

void ChartsPanel::handleChartDrop(ChartView* chart, ChartView* target, DropMode mode) {
  if (mode == DropMode::Merge) {
    mergeCharts(chart, target);
  } else {
    moveChart(chart, target, mode);
  }
}

bool ChartsPanel::event(QEvent *event) {
  bool back_button = false;
  switch (event->type()) {
    case QEvent::MouseButtonPress:
      back_button = static_cast<QMouseEvent *>(event)->button() == Qt::BackButton;
      break;
    case QEvent::NativeGesture:
      back_button = (static_cast<QNativeGestureEvent *>(event)->value() == 180);
      break;
    case QEvent::WindowDeactivate:
    case QEvent::FocusOut:
      hideHover();
    default:
      break;
  }

  if (back_button) {
    toolbar->zoom_undo_stack->undo();
    return true;  // Return true since the event has been handled
  }
  return QFrame::event(event);
}
