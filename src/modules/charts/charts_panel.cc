#include "charts_panel.h"

#include <algorithm>

#include <QApplication>
#include <QFutureSynchronizer>
#include <QMenu>
#include <QScrollBar>
#include <QToolBar>
#include <QtConcurrent>

#include "chart_view.h"
#include "modules/settings/settings.h"
#include "modules/system/stream_manager.h"

const int MAX_COLUMN_COUNT = 4;

ChartsPanel::ChartsPanel(QWidget *parent) : QFrame(parent) {
  align_timer = new QTimer(this);
  auto_scroll_timer = new QTimer(this);
  setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(0);

  setupToolbar(main_layout);
  setupTabBar(main_layout);

  // charts
  charts_container = new ChartsContainer(this);
  charts_container->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  charts_scroll = new QScrollArea(this);
  charts_scroll->viewport()->setBackgroundRole(QPalette::Base);
  charts_scroll->setFrameStyle(QFrame::NoFrame);
  charts_scroll->setWidgetResizable(true);
  charts_scroll->setWidget(charts_container);
  charts_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  main_layout->addWidget(charts_scroll);

  // init settings
  current_theme = settings.theme;
  column_count = std::clamp(settings.chart_column_count, 1, MAX_COLUMN_COUNT);
  max_chart_range = std::clamp(settings.chart_range, 1, settings.max_cached_minutes * 60);
  auto min_sec = StreamManager::stream()->minSeconds();
  display_range = std::make_pair(min_sec, min_sec + max_chart_range);
  range_slider->setValue(max_chart_range);
  updateToolBar();

  align_timer->setSingleShot(true);
  setupConnections();

  setIsDocked(true);
  newTab();
  qApp->installEventFilter(this);
  setWhatsThis(tr(R"(
    <b>Chart View</b><br />
    <b>Click</b>: Click to seek to a corresponding time.<br />
    <b>Drag</b>: Zoom into the chart.<br />
    <b>Shift + Drag</b>: Scrub through the chart to view values.<br />
    <b>Right Mouse</b>: Open the context menu.<br />
  )"));
}

void ChartsPanel::setupToolbar(QVBoxLayout *main_layout) {
  toolbar = new QToolBar(tr("Charts"), this);
  int icon_size = style()->pixelMetric(QStyle::PM_SmallIconSize);
  toolbar->setIconSize({icon_size, icon_size});

  new_plot_btn = new ToolButton("plus", tr("New Chart"));
  new_tab_btn = new ToolButton("layer-plus", tr("New Tab"));
  toolbar->addWidget(new_plot_btn);
  toolbar->addWidget(new_tab_btn);
  toolbar->addWidget(title_label = new QLabel());
  title_label->setContentsMargins(0, 0, style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing), 0);

  auto chart_type_action = toolbar->addAction("");
  QMenu* chart_type_menu = new QMenu(this);
  auto types = std::array{tr("Line"), tr("Step"), tr("Scatter")};
  for (int i = 0; i < types.size(); ++i) {
    QString type_text = types[i];
    chart_type_menu->addAction(type_text, this, [=]() {
      settings.chart_series_type = i;
      chart_type_action->setText("Type: " + type_text);
      settingChanged();
    });
  }
  chart_type_action->setText("Type: " + types[settings.chart_series_type]);
  chart_type_action->setMenu(chart_type_menu);
  qobject_cast<QToolButton*>(toolbar->widgetForAction(chart_type_action))->setPopupMode(QToolButton::InstantPopup);

  QMenu* menu = new QMenu(this);
  for (int i = 0; i < MAX_COLUMN_COUNT; ++i) {
    menu->addAction(tr("%1").arg(i + 1), [=]() { setColumnCount(i + 1); });
  }
  columns_action = toolbar->addAction("");
  columns_action->setMenu(menu);
  qobject_cast<QToolButton*>(toolbar->widgetForAction(columns_action))->setPopupMode(QToolButton::InstantPopup);

  QWidget* spacer = new QWidget(this);
  spacer->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
  toolbar->addWidget(spacer);

  range_lb_action = toolbar->addWidget(range_lb = new QLabel(this));
  range_slider = new LogSlider(1000, Qt::Horizontal, this);
  range_slider->setFixedWidth(150 * qApp->devicePixelRatio());
  range_slider->setToolTip(tr("Set the chart range"));
  range_slider->setRange(1, settings.max_cached_minutes * 60);
  range_slider->setSingleStep(1);
  range_slider->setPageStep(60);  // 1 min
  range_slider_action = toolbar->addWidget(range_slider);

  // zoom controls
  zoom_undo_stack = new QUndoStack(this);
  toolbar->addAction(undo_zoom_action = zoom_undo_stack->createUndoAction(this));
  undo_zoom_action->setIcon(utils::icon("undo-2"));
  toolbar->addAction(redo_zoom_action = zoom_undo_stack->createRedoAction(this));
  redo_zoom_action->setIcon(utils::icon("redo-2"));
  reset_zoom_action = toolbar->addWidget(reset_zoom_btn = new ToolButton("refresh-ccw", tr("Reset Zoom")));
  reset_zoom_btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

  toolbar->addWidget(remove_all_btn = new ToolButton("eraser", tr("Remove all charts")));
  toolbar->addWidget(dock_btn = new ToolButton("external-link"));
  main_layout->addWidget(toolbar);
}

void ChartsPanel::setupTabBar(QVBoxLayout *main_layout) {
  tabbar = new TabBar(this);
  tabbar->setAutoHide(true);
  tabbar->setExpanding(false);
  tabbar->setDrawBase(true);
  tabbar->setAcceptDrops(true);
  tabbar->setChangeCurrentOnDrag(true);
  tabbar->setUsesScrollButtons(true);
  main_layout->addWidget(tabbar);
}

void ChartsPanel::setupConnections() {
  connect(align_timer, &QTimer::timeout, this, &ChartsPanel::alignCharts);
  connect(auto_scroll_timer, &QTimer::timeout, this, &ChartsPanel::doAutoScroll);
  connect(GetDBC(), &dbc::Manager::DBCFileChanged, this, &ChartsPanel::removeAll);
  connect(&StreamManager::instance(), &StreamManager::eventsMerged, this, &ChartsPanel::eventsMerged);
  connect(&StreamManager::instance(), &StreamManager::snapshotsUpdated, this, &ChartsPanel::updateState);
  connect(&StreamManager::instance(), &StreamManager::seeking, this, &ChartsPanel::updateState);
  connect(&StreamManager::instance(), &StreamManager::timeRangeChanged, this, &ChartsPanel::timeRangeChanged);
  connect(range_slider, &QSlider::valueChanged, this, &ChartsPanel::setMaxChartRange);
  connect(new_plot_btn, &QToolButton::clicked, this, &ChartsPanel::newChart);
  connect(remove_all_btn, &QToolButton::clicked, this, &ChartsPanel::removeAll);
  connect(reset_zoom_btn, &QToolButton::clicked, this, &ChartsPanel::zoomReset);
  connect(&settings, &Settings::changed, this, &ChartsPanel::settingChanged);
  connect(new_tab_btn, &QToolButton::clicked, this, &ChartsPanel::newTab);
  connect(this, &ChartsPanel::seriesChanged, this, &ChartsPanel::updateTabBar);
  connect(tabbar, &QTabBar::tabCloseRequested, this, &ChartsPanel::removeTab);
  connect(tabbar, &QTabBar::currentChanged, [this](int index) {
    if (index != -1) updateLayout(true);
  });
  connect(dock_btn, &QToolButton::clicked, this, &ChartsPanel::toggleChartsDocking);
}

void ChartsPanel::newTab() {
  static int tab_unique_id = 0;
  int idx = tabbar->addTab("");
  tabbar->setTabData(idx, tab_unique_id++);
  tabbar->setCurrentIndex(idx);
  updateTabBar();
}

void ChartsPanel::removeTab(int index) {
  int id = tabbar->tabData(index).toInt();
  for (auto &c : tab_charts[id]) {
    removeChart(c);
  }
  tab_charts.erase(id);
  tabbar->removeTab(index);
  updateTabBar();
}

void ChartsPanel::updateTabBar() {
  for (int i = 0; i < tabbar->count(); ++i) {
    const auto &charts_in_tab = tab_charts[tabbar->tabData(i).toInt()];
    tabbar->setTabText(i, QString("Tab %1 (%2)").arg(i + 1).arg(charts_in_tab.count()));
  }
}

void ChartsPanel::eventsMerged(const MessageEventsMap &new_events) {
  QFutureSynchronizer<void> future_synchronizer;
  for (auto c : charts) {
    future_synchronizer.addFuture(QtConcurrent::run(c->chart_, &Chart::updateSeries, nullptr, &new_events));
  }
}

void ChartsPanel::timeRangeChanged(const std::optional<std::pair<double, double>> &time_range) {
  updateToolBar();
  updateState();
}

void ChartsPanel::zoomReset() {
  StreamManager::stream()->setTimeRange(std::nullopt);
  zoom_undo_stack->clear();
}

QRect ChartsPanel::chartVisibleRect(ChartView *chart) {
  const QRect visible_rect(-charts_container->pos(), charts_scroll->viewport()->size());
  return chart->rect().intersected(QRect(chart->mapFrom(charts_container, visible_rect.topLeft()), visible_rect.size()));
}

void ChartsPanel::showValueTip(double sec) {
  emit showTip(sec);
  if (sec < 0 && !value_tip_visible_) return;

  value_tip_visible_ = sec >= 0;
  for (auto c : currentCharts()) {
    value_tip_visible_ ? c->showTip(sec) : c->hideTip();
  }
}

void ChartsPanel::updateState() {
  if (charts.isEmpty()) return;

  auto *can = StreamManager::stream();
  const auto &time_range = can->timeRange();
  const double cur_sec = can->currentSec();
  if (!time_range.has_value()) {
    double pos = (cur_sec - display_range.first) / std::max<float>(1.0, max_chart_range);
    if (pos < 0 || pos > 0.8) {
      display_range.first = std::max(can->minSeconds(), cur_sec - max_chart_range * 0.1);
    }
    double max_sec = std::min(display_range.first + max_chart_range, can->maxSeconds());
    display_range.first = std::max(can->minSeconds(), max_sec - max_chart_range);
    display_range.second = display_range.first + max_chart_range;
  }

  const auto &range = time_range ? *time_range : display_range;
  for (auto c : charts) {
    c->updatePlot(cur_sec, range.first, range.second);
  }
}

void ChartsPanel::setMaxChartRange(int value) {
  max_chart_range = settings.chart_range = range_slider->value();
  updateToolBar();
  updateState();
}

void ChartsPanel::setIsDocked(bool docked) {
  is_docked = docked;
  dock_btn->setIcon(is_docked ? "external-link" : "dock");
  dock_btn->setToolTip(is_docked ? tr("Float the charts window") : tr("Dock the charts window"));
}

void ChartsPanel::updateToolBar() {
  title_label->setText(tr("Charts: %1").arg(charts.size()));
  columns_action->setText(tr("Columns: %1").arg(column_count));
  range_lb->setText(utils::formatSeconds(max_chart_range));

  auto *can = StreamManager::stream();
  bool is_zoomed = can->timeRange().has_value();
  range_lb_action->setVisible(!is_zoomed);
  range_slider_action->setVisible(!is_zoomed);
  undo_zoom_action->setVisible(is_zoomed);
  redo_zoom_action->setVisible(is_zoomed);
  reset_zoom_action->setVisible(is_zoomed);
  reset_zoom_btn->setText(is_zoomed ? tr("%1-%2").arg(can->timeRange()->first, 0, 'f', 2).arg(can->timeRange()->second, 0, 'f', 2) : "");
  remove_all_btn->setEnabled(!charts.isEmpty());
}

void ChartsPanel::settingChanged() {
  if (std::exchange(current_theme, settings.theme) != current_theme) {
    undo_zoom_action->setIcon(utils::icon("undo-2"));
    redo_zoom_action->setIcon(utils::icon("redo-2"));
    auto theme = utils::isDarkTheme() ? QChart::QChart::ChartThemeDark : QChart::ChartThemeLight;
    for (auto c : charts) {
      c->chart_->setTheme(theme);
    }
  }
  if (range_slider->maximum() != settings.max_cached_minutes * 60) {
    range_slider->setRange(1, settings.max_cached_minutes * 60);
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

ChartView *ChartsPanel::createChart(int pos) {
  auto chart = new ChartView(StreamManager::stream()->timeRange().value_or(display_range), this);
  connect(chart, &ChartView::axisYLabelWidthChanged, align_timer, qOverload<>(&QTimer::start));
  pos = std::clamp(pos, 0, charts.size());
  charts.insert(pos, chart);
  currentCharts().insert(pos, chart);
  updateLayout(true);
  updateToolBar();
  return chart;
}

void ChartsPanel::showChart(const MessageId &id, const dbc::Signal *sig, bool show, bool merge) {
  ChartView *c = findChart(id, sig);
  if (show && !c) {
    c = merge && currentCharts().size() > 0 ? currentCharts().front() : createChart();
    c->chart_->addSignal(id, sig);
    updateState();
  } else if (!show && c) {
    c->chart_->removeIf([&](auto &s) { return s.msg_id == id && s.sig == sig; });
  }
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
    column_count = settings.chart_column_count = n;
    updateToolBar();
    updateLayout();
  }
}

void ChartsPanel::updateLayout(bool force) {
  auto charts_layout = charts_container->charts_layout;
  int n = MAX_COLUMN_COUNT;
  for (; n > 1; --n) {
    if ((n * CHART_MIN_WIDTH + (n - 1) * charts_layout->horizontalSpacing()) < charts_layout->geometry().width()) break;
  }

  bool show_column_cb = n > 1;
  columns_action->setVisible(show_column_cb);

  n = std::min(column_count, n);
  auto &current_charts = currentCharts();
  if ((current_charts.size() != charts_layout->count() || n != current_column_count) || force) {
    current_column_count = n;
    charts_container->setUpdatesEnabled(false);
    for (auto c : charts) {
      c->setVisible(false);
    }
    for (int i = 0; i < current_charts.size(); ++i) {
      charts_layout->addWidget(current_charts[i], i / n, i % n);
      if (current_charts[i]->chart_->sigs_.empty()) {
        // the chart will be resized after add signal. delay setVisible to reduce flicker.
        QTimer::singleShot(0, current_charts[i], [c = current_charts[i]]() { c->setVisible(true); });
      } else {
        current_charts[i]->setVisible(true);
      }
    }
    charts_container->setUpdatesEnabled(true);
  }

  if (charts.isEmpty()) {
    charts_container->setMinimumHeight(charts_scroll->viewport()->height());
  } else {
    charts_container->setMinimumHeight(0);
  }
  charts_container->update();
}

void ChartsPanel::startAutoScroll() {
  auto_scroll_timer->start(50);
}

void ChartsPanel::stopAutoScroll() {
  auto_scroll_timer->stop();
  auto_scroll_count = 0;
}

void ChartsPanel::doAutoScroll() {
  QScrollBar *scroll = charts_scroll->verticalScrollBar();
  if (auto_scroll_count < scroll->pageStep()) {
    ++auto_scroll_count;
  }

  int value = scroll->value();
  QPoint pos = charts_scroll->viewport()->mapFromGlobal(QCursor::pos());
  QRect area = charts_scroll->viewport()->rect();

  if (pos.y() - area.top() < settings.chart_height / 2) {
    scroll->setValue(value - auto_scroll_count);
  } else if (area.bottom() - pos.y() < settings.chart_height / 2) {
    scroll->setValue(value + auto_scroll_count);
  }
  bool vertical_unchanged = value == scroll->value();
  if (vertical_unchanged) {
    stopAutoScroll();
  } else {
    // mouseMoveEvent to updates the drag-selection rectangle
    const QPoint globalPos = charts_scroll->viewport()->mapToGlobal(pos);
    const QPoint windowPos = charts_scroll->window()->mapFromGlobal(globalPos);
    QMouseEvent mm(QEvent::MouseMove, pos, windowPos, globalPos,
                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier, Qt::MouseEventSynthesizedByQt);
    QApplication::sendEvent(charts_scroll->viewport(), &mm);
  }
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

void ChartsPanel::removeChart(ChartView *chart) {
  charts.removeOne(chart);
  chart->deleteLater();
  for (auto &[_, list] : tab_charts) {
    list.removeOne(chart);
  }
  updateToolBar();
  updateLayout(true);
  alignCharts();
  emit seriesChanged();
}

void ChartsPanel::removeAll() {
  while (tabbar->count() > 1) {
    tabbar->removeTab(1);
  }
  tab_charts.clear();

  if (!charts.isEmpty()) {
    for (auto c : charts) {
      delete c;
    }
    charts.clear();
    emit seriesChanged();
  }
  zoomReset();
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

bool ChartsPanel::eventFilter(QObject *o, QEvent *e) {
  if (!value_tip_visible_) return false;

  if (e->type() == QEvent::MouseMove) {
    bool on_tip = qobject_cast<TipLabel *>(o) != nullptr;
    auto global_pos = static_cast<QMouseEvent *>(e)->globalPos();

    for (const auto &c : charts) {
      auto local_pos = c->mapFromGlobal(global_pos);
      if (c->chart()->plotArea().contains(local_pos)) {
        if (on_tip) {
          showValueTip(c->secondsAtPoint(local_pos));
        }
        return false;
      }
    }

    showValueTip(-1);
  } else if (e->type() == QEvent::Wheel) {
    if (auto tip = qobject_cast<TipLabel *>(o)) {
      // Forward the event to the parent widget
      QCoreApplication::sendEvent(tip->parentWidget(), e);
    }
  }
  return false;
}

bool ChartsPanel::event(QEvent *event) {
  bool back_button = false;
  switch (event->type()) {
    case QEvent::Resize:
      updateLayout();
      break;
    case QEvent::MouseButtonPress:
      back_button = static_cast<QMouseEvent *>(event)->button() == Qt::BackButton;
      break;
    case QEvent::NativeGesture:
      back_button = (static_cast<QNativeGestureEvent *>(event)->value() == 180);
      break;
    case QEvent::WindowDeactivate:
    case QEvent::FocusOut:
      showValueTip(-1);
    default:
      break;
  }

  if (back_button) {
    zoom_undo_stack->undo();
    return true;  // Return true since the event has been handled
  }
  return QFrame::event(event);
}
