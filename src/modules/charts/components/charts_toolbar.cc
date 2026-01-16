#include "charts_toolbar.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QMenu>
#include <QStyle>

#include "modules/settings/settings.h"

ChartsToolBar::ChartsToolBar(QWidget* parent) : QToolBar(tr("Charts"), parent) {
  setMovable(false);
  setFloatable(false);

  const int icon_size = style()->pixelMetric(QStyle::PM_SmallIconSize);
  setIconSize({icon_size, icon_size});

  createActions();

  int max_chart_range = std::clamp(settings.chart_range, 1, settings.max_cached_minutes * 60);
  range_slider->setValue(max_chart_range);
  setIsDocked(true);

  updateState(0);

  connect(reset_zoom_btn, &QToolButton::clicked, this, &ChartsToolBar::zoomReset);
  connect(&settings, &Settings::changed, this, &ChartsToolBar::settingChanged);
  connect(range_slider, &QSlider::valueChanged, this, [=](int value) {
    settings.chart_range = value;
    range_lb->setText(utils::formatSeconds(value));
    emit rangeChanged(value);
  });
}

void ChartsToolBar::createActions() {
  new_plot_btn = new ToolButton("plus", tr("New Chart"));
  new_tab_btn = new ToolButton("layer-plus", tr("New Tab"));
  addWidget(new_plot_btn);
  addWidget(new_tab_btn);
  addWidget(title_label = new QLabel());
  title_label->setContentsMargins(0, 0, style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing), 0);

  createTypeMenu();
  createColumnMenu();

  QWidget* spacer = new QWidget(this);
  spacer->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
  addWidget(spacer);

  setupZoomControls();

  addSeparator();

  addWidget(remove_all_btn = new ToolButton("eraser", tr("Remove all charts")));
  addWidget(dock_btn = new ToolButton("external-link"));
}

void ChartsToolBar::createTypeMenu() {
  auto chart_type_action = addAction("");
  QMenu* menu = new QMenu(this);
  auto types = std::array{tr("Line"), tr("Step"), tr("Scatter")};
  for (int i = 0; i < types.size(); ++i) {
    QString type_text = types[i];
    menu->addAction(type_text, this, [=]() {
      settings.chart_series_type = i;
      chart_type_action->setText("Type: " + type_text);
      emit seriesTypeChanged(i);
    });
  }
  chart_type_action->setText("Type: " + types[settings.chart_series_type]);
  chart_type_action->setMenu(menu);
  qobject_cast<QToolButton*>(widgetForAction(chart_type_action))->setPopupMode(QToolButton::InstantPopup);
}

void ChartsToolBar::createColumnMenu() {
  QMenu* menu = new QMenu(this);
  for (int i = 0; i < MAX_COLUMN_COUNT; ++i) {
    menu->addAction(tr("%1").arg(i + 1), [=]() { 
      settings.chart_column_count = i + 1;
      columns_action->setText(tr("Columns: %1").arg(i + 1));
      emit columnCountChanged(i + 1); });
  }
  columns_action = addAction("");
  columns_action->setMenu(menu);
  qobject_cast<QToolButton*>(widgetForAction(columns_action))->setPopupMode(QToolButton::InstantPopup);
}

void ChartsToolBar::setupZoomControls() {
  range_lb_action = addWidget(range_lb = new QLabel(this));
  range_slider = new LogSlider(1000, Qt::Horizontal, this);
  range_slider->setFixedWidth(150 * qApp->devicePixelRatio());
  range_slider->setToolTip(tr("Set the chart range"));
  range_slider->setRange(1, settings.max_cached_minutes * 60);
  range_slider->setSingleStep(1);
  range_slider->setPageStep(60);  // 1 min
  range_slider_action = addWidget(range_slider);

  // zoom controls
  zoom_undo_stack = new QUndoStack(this);
  addAction(undo_zoom_action = zoom_undo_stack->createUndoAction(this));
  undo_zoom_action->setIcon(utils::icon("undo-2"));
  addAction(redo_zoom_action = zoom_undo_stack->createRedoAction(this));
  redo_zoom_action->setIcon(utils::icon("redo-2"));
  reset_zoom_action = addWidget(reset_zoom_btn = new ToolButton("refresh-ccw", tr("Reset Zoom")));
  reset_zoom_btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
}

void ChartsToolBar::updateState(int chart_count) {
  title_label->setText(tr("Charts: %1").arg(chart_count));
  columns_action->setText(tr("Columns: %1").arg(settings.chart_column_count));
  range_lb->setText(utils::formatSeconds(settings.chart_range));

  auto* stream = StreamManager::stream();
  bool is_zoomed = stream->timeRange().has_value();
  range_lb_action->setVisible(!is_zoomed);
  range_slider_action->setVisible(!is_zoomed);
  undo_zoom_action->setVisible(is_zoomed);
  redo_zoom_action->setVisible(is_zoomed);
  reset_zoom_action->setVisible(is_zoomed);
  reset_zoom_btn->setText(is_zoomed ? tr("%1-%2").arg(stream->timeRange()->first, 0, 'f', 2).arg(stream->timeRange()->second, 0, 'f', 2) : "");
  remove_all_btn->setEnabled(chart_count > 0);
}

void ChartsToolBar::setIsDocked(bool docked) {
  is_docked = docked;
  dock_btn->setIcon(is_docked ? "external-link" : "dock");
  dock_btn->setToolTip(is_docked ? tr("Float Window") : tr("Dock Window"));
}

void ChartsToolBar::zoomReset() {
  StreamManager::stream()->setTimeRange(std::nullopt);
  zoom_undo_stack->clear();
}

void ChartsToolBar::settingChanged() {
  undo_zoom_action->setIcon(utils::icon("undo-2"));
  redo_zoom_action->setIcon(utils::icon("redo-2"));
  int max_sec = settings.max_cached_minutes * 60;
  if (range_slider->maximum() != max_sec) {
    range_slider->setRange(1, max_sec);
  }
}
