#include "charts_tab_manager.h"

#include "modules/charts/chart_view.h"

ChartsTabManager::ChartsTabManager(QWidget* parent)
    : QObject(parent) {
  tabbar_ = new TabBar(parent);
  tabbar_->setAutoHide(true);
  tabbar_->setExpanding(false);
  tabbar_->setDrawBase(true);
  tabbar_->setAcceptDrops(true);
  tabbar_->setChangeCurrentOnDrag(true);
  tabbar_->setUsesScrollButtons(true);

  connect(tabbar_, &TabBar::tabCloseRequested, this, &ChartsTabManager::handleTabClose);
  connect(tabbar_, &QTabBar::currentChanged, this, [this](int index) {
    if (index != -1) emit currentTabChanged(index);
  });

  addTab();
}

void ChartsTabManager::addTab() {
  int idx = tabbar_->addTab("");
  int id = next_tab_id_++;
  tabbar_->setTabData(idx, id);
  tab_charts_[id] = QList<ChartView*>();  // Initialize map entry
  tabbar_->setCurrentIndex(idx);
  updateLabels();
}

void ChartsTabManager::handleTabClose(int index) {
  if (tabbar_->count() <= 1) return;  // Keep at least one tab or handle empty state

  int id = tabbar_->tabData(index).toInt();
  QList<ChartView*> chartsToRemove = tab_charts_.take(id);

  // NOTIFY the Panel. The Panel will iterate this list and call delete.
  emit tabAboutToBeRemoved(chartsToRemove);

  tabbar_->removeTab(index);
  updateLabels();
}

void ChartsTabManager::insertChart(int pos, ChartView* chart) {
  auto &current_charts = currentCharts();
  pos = std::clamp(pos, 0, current_charts.size());
  current_charts.insert(pos, chart);
  updateLabels();
}

void ChartsTabManager::removeChart(ChartView* chart) {
  for (auto& list : tab_charts_) {
    if (list.removeOne(chart)) {
      updateLabels();
      break;
    }
  }
}

void ChartsTabManager::clear() {
  tab_charts_.clear();
  tabbar_->blockSignals(true);
  while (tabbar_->count() > 1) tabbar_->removeTab(1);
  tabbar_->setCurrentIndex(0);
  tabbar_->blockSignals(false);
  updateLabels();
}

QList<ChartView*>& ChartsTabManager::currentCharts() {
  static QList<ChartView*> empty;
  int idx = tabbar_->currentIndex();
  if (idx == -1) return empty;
  return tab_charts_[tabbar_->tabData(idx).toInt()];
}

void ChartsTabManager::updateLabels() {
  for (int i = 0; i < tabbar_->count(); ++i) {
    int id = tabbar_->tabData(i).toInt();
    int count = tab_charts_[id].count();
    tabbar_->setTabText(i, tr("Tab %1 (%2)").arg(i + 1).arg(count));
  }
}
