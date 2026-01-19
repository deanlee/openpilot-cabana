#include "charts_toolbar.h"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QStyle>

#include "charts_toolbar.h"
#include "modules/settings/settings.h"

ChartsToolBar::ChartsToolBar(QWidget* parent) : QWidget(parent) {
  QHBoxLayout* hl = new QHBoxLayout(this);
  // Match Cabana standard header margins
  int h_margin = style()->pixelMetric(QStyle::PM_ToolBarItemMargin);
  hl->setContentsMargins(h_margin, 2, h_margin, 2);
  hl->setSpacing(style()->pixelMetric(QStyle::PM_ToolBarItemSpacing));

  createActions(hl);

  // Preserve original config logic
  int max_chart_range = std::clamp(settings.chart_range, 1, settings.max_cached_minutes * 60);
  range_slider->setValue(max_chart_range);
  setIsDocked(true);
  updateState(0);

  // Signal Connections
  connect(reset_zoom_btn, &QToolButton::clicked, this, &ChartsToolBar::zoomReset);
  connect(&settings, &Settings::changed, this, &ChartsToolBar::settingChanged);
  connect(range_slider, &QSlider::valueChanged, this, [=](int value) {
    settings.chart_range = value;
    range_lb->setText(utils::formatSeconds(value));
    emit rangeChanged(value);
  });
}

void ChartsToolBar::createActions(QHBoxLayout* hl) {
  hl->addWidget(new_plot_btn = new ToolButton("plus", tr("Add New Chart")));
  hl->addWidget(new_tab_btn = new ToolButton("layer-plus", tr("New Tab")));

  hl->addWidget(title_label = new QLabel());
  title_label->setStyleSheet("font-weight: bold;");

  // 1. Chart Type Menu (Preserving InstantPopup feature)
  chart_type_btn = new ToolButton("line-chart", "");
  chart_type_btn->setPopupMode(QToolButton::InstantPopup);
  chart_type_btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  chart_type_btn->setToolTip(tr("Chart Drawing Style\nChange how signals are visualized."));
  createTypeMenu();
  hl->addWidget(chart_type_btn);

  // 2. Column Menu (Preserving InstantPopup feature)
  columns_btn = new ToolButton("columns", "");
  columns_btn->setPopupMode(QToolButton::InstantPopup);
  columns_btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  columns_btn->setToolTip(tr("Grid Layout\nSet number of columns."));
  createColumnMenu();
  hl->addWidget(columns_btn);

  hl->addStretch(1);

  setupZoomControls(hl);

  hl->addWidget(createVLine(this), 0, Qt::AlignCenter);

  hl->addWidget(remove_all_btn = new ToolButton("eraser", tr("Remove All Charts")));
  hl->addWidget(dock_btn = new ToolButton("external-link"));
}

void ChartsToolBar::createTypeMenu() {
  QMenu* menu = new QMenu(this);
  auto types = std::array{tr("Line"), tr("Step"), tr("Scatter")};
  for (int i = 0; i < types.size(); ++i) {
    menu->addAction(types[i], this, [=, type_text = types[i]]() {
      settings.chart_series_type = i;
      chart_type_btn->setText(tr("Type: %1").arg(type_text));
      emit seriesTypeChanged(i);
    });
  }
  chart_type_btn->setMenu(menu);
  chart_type_btn->setText(tr("Type: %1").arg(types[settings.chart_series_type]));
}

void ChartsToolBar::createColumnMenu() {
  QMenu* menu = new QMenu(this);
  for (int i = 0; i < MAX_COLUMN_COUNT; ++i) {
    menu->addAction(tr("%1").arg(i + 1), [=, count = i + 1]() {
      settings.chart_column_count = count;
      columns_btn->setText(tr("Columns: %1").arg(count));
      emit columnCountChanged(count);
    });
  }
  columns_btn->setMenu(menu);
  columns_btn->setText(tr("Columns: %1").arg(settings.chart_column_count));
}

void ChartsToolBar::setupZoomControls(QHBoxLayout* hl) {
  hl->addWidget(range_lb = new QLabel(this));

  range_slider = new LogSlider(1000, Qt::Horizontal, this);
  range_slider->setMaximumWidth(200);
  range_slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  range_slider->setRange(1, settings.max_cached_minutes * 60);
  range_slider->setSingleStep(1);
  range_slider->setPageStep(60);
  hl->addWidget(range_slider, 2);

  zoom_undo_stack = new QUndoStack(this);

  // Undo/Redo Buttons
  hl->addWidget(undo_btn = new ToolButton("undo-2", tr("Undo last zoom")));
  hl->addWidget(redo_btn = new ToolButton("redo-2", tr("Redo last zoom")));

  undo_btn->setEnabled(zoom_undo_stack->canUndo());
  redo_btn->setEnabled(zoom_undo_stack->canRedo());

  connect(zoom_undo_stack, &QUndoStack::canUndoChanged, undo_btn, &QToolButton::setEnabled);
  connect(zoom_undo_stack, &QUndoStack::canRedoChanged, redo_btn, &QToolButton::setEnabled);
  connect(undo_btn, &QToolButton::clicked, zoom_undo_stack, &QUndoStack::undo);
  connect(redo_btn, &QToolButton::clicked, zoom_undo_stack, &QUndoStack::redo);

  // Reset Zoom (Text Beside Icon config)
  hl->addWidget(reset_zoom_btn = new ToolButton("refresh-ccw", tr("Reset Zoom")));
  reset_zoom_btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
}

void ChartsToolBar::updateState(int chart_count) {
  title_label->setText(tr("Charts: %1").arg(chart_count));
  columns_btn->setText(tr("Columns: %1").arg(settings.chart_column_count));
  range_lb->setText(utils::formatSeconds(settings.chart_range));

  auto* stream = StreamManager::stream();
  bool is_zoomed = stream->timeRange().has_value();
  range_lb->setVisible(!is_zoomed);
  range_slider->setVisible(!is_zoomed);
  undo_btn->setVisible(is_zoomed);
  redo_btn->setVisible(is_zoomed);
  reset_zoom_btn->setVisible(is_zoomed);
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
  undo_btn->setIcon("undo-2");
  redo_btn->setIcon("redo-2");
  int max_sec = settings.max_cached_minutes * 60;
  if (range_slider->maximum() != max_sec) {
    range_slider->setRange(1, max_sec);
  }
}
