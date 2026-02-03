#include "charts_tab_manager.h"

ChartsTabManager::ChartsTabManager(QWidget* parent) : QObject(parent) {
  tabbar_ = new TabBar(parent);
  tabbar_->setAutoHide(true);
  tabbar_->setExpanding(false);
  tabbar_->setDrawBase(true);
  tabbar_->setAcceptDrops(true);
  tabbar_->setChangeCurrentOnDrag(true);
  tabbar_->setUsesScrollButtons(true);

  connect(tabbar_, &QTabBar::currentChanged, this, [this](int index) {
    if (index != -1) emit tabActivated(tabbar_->tabData(index).toInt());
  });
  connect(tabbar_, &QTabBar::tabCloseRequested, this, [this](int index) {
    if (tabbar_->count() > 1) emit tabDeleted(tabbar_->tabData(index).toInt());
    tabbar_->removeTab(index);
  });
}

int ChartsTabManager::addTab(const QString& name) {
  int id = next_tab_id_++;
  QString title = name.isEmpty() ? tr("Tab %1").arg(tabbar_->count() + 1) : name;
  int idx = tabbar_->addTab(title);
  tabbar_->setTabData(idx, id);
  tabbar_->setCurrentIndex(idx);
  return id;
}

void ChartsTabManager::setTabChartCount(int tabId, int count) {
  for (int i = 0; i < tabbar_->count(); ++i) {
    if (tabbar_->tabData(i).toInt() == tabId) {
      tabbar_->setTabText(i, tr("Tab %1").arg(count));
      break;
    }
  }
}
int ChartsTabManager::currentTabId() const {
  int idx = tabbar_->currentIndex();
  return (idx != -1) ? tabbar_->tabData(idx).toInt() : -1;
}
