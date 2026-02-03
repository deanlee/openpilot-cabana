#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QVariant>

#include "widgets/common.h"

class ChartsTabManager : public QObject {
  Q_OBJECT
 public:
  TabBar* tabbar_;

  explicit ChartsTabManager(QWidget* parent);
  int addTab(const QString& name = "");
  void setTabChartCount(int tabId, int count);
  int currentTabId() const;

 signals:
  void tabActivated(int tabId);
  void tabDeleted(int tabId);

 private:
  int next_tab_id_ = 0;
};
