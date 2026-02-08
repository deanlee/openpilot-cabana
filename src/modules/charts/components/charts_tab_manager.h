#pragma once

#include <QList>
#include <QMap>
#include <QObject>

#include "modules/charts/chart_view.h"
#include "widgets/common.h"

class ChartsTabManager : public QObject {
  Q_OBJECT
 public:
  ChartsTabManager(QWidget* parent);
  QList<ChartView*>& currentCharts();
  void addTab();
  void clear();
  void insertChart(int pos, ChartView* chart);
  void removeChart(ChartView* chart);
  TabBar* tabbar() const { return tabbar_; }
  void updateLabels();
  QMap<int, QList<ChartView*>> tab_charts_;  // Stores associations, not ownership

 signals:
  void currentTabChanged(int index);
  void tabAboutToBeRemoved(QList<ChartView*> charts);

 private slots:
  void handleTabClose(int index);

 private:
  TabBar* tabbar_;
  int next_tab_id_ = 0;
};
