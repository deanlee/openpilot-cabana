#pragma once

#include <QSet>
#include <QStackedWidget>
#include <QTimer>

#include "components/charts_container.h"
#include "components/charts_empty_view.h"
#include "components/charts_scroll_area.h"
#include "components/charts_tab_manager.h"
#include "components/charts_toolbar.h"
#include "core/dbc/dbc_manager.h"
#include "core/streams/abstract_stream.h"
#include "modules/system/stream_manager.h"
#include "signal_picker.h"
#include "widgets/common.h"

class ChartView;

class ChartsPanel : public QFrame {
  Q_OBJECT

 public:
  ChartsPanel(QWidget* parent = nullptr);
  void showChart(const MessageId& id, const dbc::Signal* sig, bool show, bool merge);
  void mergeCharts(ChartView* chart, ChartView* target);
  void moveChart(ChartView* chart, ChartView* target, DropMode mode);
  const QMap<MessageId, QSet<const dbc::Signal*>> getChartedSignals() const;
  inline bool hasSignal(const MessageId& id, const dbc::Signal* sig) { return findChart(id, sig) != nullptr; }
  QStringList serializeChartIds() const;
  void restoreChartsFromIds(const QStringList& chart_ids);
  inline ChartsToolBar* getToolBar() const { return toolbar; }

 public slots:
  void setColumnCount(int n);
  void removeAll();
  void timeRangeChanged(const std::optional<std::pair<double, double>>& time_range);

 signals:
  void toggleChartsDocking();
  void seriesChanged();
  void showCursor(double seconds);

 private:
  void changeEvent(QEvent* ev) override;
  bool eventFilter(QObject* obj, QEvent* event) override;
  void setupConnections();
  QSize minimumSizeHint() const override;
  bool event(QEvent* event) override;
  void alignCharts();
  void newChart();
  void handleChartDrop(ChartView* chart, ChartView* target, DropMode mode);
  ChartView* createChart(int pos = 0);
  void removeChart(ChartView* chart);
  void removeCharts(QList<ChartView*> charts_to_remove);
  void splitChart(ChartView* src_view);
  void eventsMerged(const MessageEventsMap& new_events);
  void updateState();
  void setMaxChartRange(int value);
  void updateLayout(bool force = false);
  void settingChanged();
  void updateHover(double time);
  void updateHoverFromCursor();
  void hideHover();
  ChartView* findChart(const MessageId& id, const dbc::Signal* sig);

  // --- UI Components ---
  ChartsToolBar* toolbar = nullptr;
  ChartsTabManager* tab_manager_ = nullptr;
  QStackedWidget* stack_ = nullptr;
  ChartsScrollArea* scroll_area_ = nullptr;
  ChartsEmptyView* empty_view_ = nullptr;

  // --- Data & State ---
  QList<ChartView*> charts;  // Total ownership of all charts
  uint32_t max_chart_range = 0;
  std::pair<double, double> display_range;

  // --- Layout & Theme State ---
  int column_count = 1;
  int current_column_count = 0;
  int current_theme = 0;
  double hover_time_ = -1;

  // --- Utilities ---
  QTimer* align_timer = nullptr;

  friend class ChartView;
  friend class ChartsContainer;
  friend class ChartsToolBar;
};
