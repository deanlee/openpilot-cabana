#pragma once

#include <QLabel>
#include <QScrollArea>
#include <QSet>
#include <QTimer>
#include <unordered_map>
#include <utility>

#include "charts_container.h"
#include "components/charts_toolbar.h"
#include "core/dbc/dbc_manager.h"
#include "core/streams/abstract_stream.h"
#include "modules/system/stream_manager.h"
#include "signal_picker.h"
#include "widgets/common.h"

const int CHART_MIN_WIDTH = 300;
const QString CHART_MIME_TYPE = "application/x-cabanachartview";

class ChartView;

class ChartsPanel : public QFrame {
  Q_OBJECT

public:
  ChartsPanel(QWidget *parent = nullptr);
  void showChart(const MessageId &id, const dbc::Signal *sig, bool show, bool merge);
  const QSet<const dbc::Signal *> getChartedSignals() const;
  inline bool hasSignal(const MessageId &id, const dbc::Signal *sig) { return findChart(id, sig) != nullptr; }
  QStringList serializeChartIds() const;
  void restoreChartsFromIds(const QStringList &chart_ids);
  inline ChartsToolBar *getToolBar() const { return toolbar; }

public slots:
  void setColumnCount(int n);
  void removeAll();
  void timeRangeChanged(const std::optional<std::pair<double, double>> &time_range);

signals:
  void toggleChartsDocking();
  void seriesChanged();
  void showTip(double seconds);

private:
  void setupTabBar(QVBoxLayout *main_layout);
  void setupConnections();
  QSize minimumSizeHint() const override;
  bool event(QEvent *event) override;
  void alignCharts();
  void newChart();
  ChartView *createChart(int pos = 0);
  void removeChart(ChartView *chart);
  void splitChart(ChartView *src_view);
  QRect chartVisibleRect(ChartView *chart);
  void eventsMerged(const MessageEventsMap &new_events);
  void updateState();
  void startAutoScroll();
  void stopAutoScroll();
  void doAutoScroll();
  void updateTabBar();
  void setMaxChartRange(int value);
  void updateLayout(bool force = false);
  void settingChanged();
  void showValueTip(double sec);
  bool eventFilter(QObject *obj, QEvent *event) override;
  void newTab();
  void removeTab(int index);
  inline QList<ChartView *> &currentCharts() { return tab_charts[tabbar->tabData(tabbar->currentIndex()).toInt()]; }
  ChartView *findChart(const MessageId &id, const dbc::Signal *sig);

  ChartsToolBar *toolbar;
  QList<ChartView *> charts;
  std::unordered_map<int, QList<ChartView *>> tab_charts;
  TabBar *tabbar;
  ChartsContainer *charts_container;
  QScrollArea *charts_scroll;
  uint32_t max_chart_range = 0;
  std::pair<double, double> display_range;
  int column_count = 1;
  int current_column_count = 0;
  int auto_scroll_count = 0;
  QTimer *auto_scroll_timer;
  QTimer *align_timer;
  int current_theme = 0;
  bool value_tip_visible_ = false;
  friend class ChartView;
  friend class ChartsContainer;
  friend class ChartsToolBar;
};
