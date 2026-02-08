#pragma once

#include <QHBoxLayout>
#include <QLabel>
#include <QUndoCommand>
#include <QUndoStack>
#include <QWidget>
#include <optional>

#include "modules/system/stream_manager.h"
#include "widgets/common.h"
#include "widgets/tool_button.h"

constexpr int MAX_COLUMN_COUNT = 4;

class ChartsToolBar : public QWidget {
  Q_OBJECT

 public:
  ChartsToolBar(QWidget* parent);
  void setIsDocked(bool docked);
  void updateState(int chart_count);
  void zoomReset();

 signals:
  void columnCountChanged(int n);
  void seriesTypeChanged(int type);
  void rangeChanged(int range_seconds);

 protected:
  void settingChanged();
  void createActions(QHBoxLayout* hl);  // Pass layout to populate
  void createTypeMenu();
  void createColumnMenu();
  void setupZoomControls(QHBoxLayout* hl);

 protected:
  // UI Elements
  QLabel* title_label;
  QLabel* range_lb;
  LogSlider* range_slider;

  // Custom ToolButtons (Replaces QActions)
  ToolButton* chart_type_btn;
  ToolButton* columns_btn;
  ToolButton* undo_btn;
  ToolButton* redo_btn;
  ToolButton* reset_zoom_btn;
  ToolButton* new_plot_btn;
  ToolButton* new_tab_btn;
  ToolButton* remove_all_btn;
  ToolButton* dock_btn;
  QUndoStack* zoom_undo_stack;

  bool is_docked = true;
  friend class ChartsPanel;
};

class ZoomCommand : public QUndoCommand {
 public:
  ZoomCommand(std::pair<double, double> range) : range(range), QUndoCommand() {
    prev_range = StreamManager::stream()->timeRange();
    setText(QObject::tr("Zoom to %1-%2").arg(range.first, 0, 'f', 2).arg(range.second, 0, 'f', 2));
  }
  void undo() override { StreamManager::stream()->setTimeRange(prev_range); }
  void redo() override { StreamManager::stream()->setTimeRange(range); }
  std::optional<std::pair<double, double>> prev_range, range;
};
