#pragma once

#include <QComboBox>
#include <QDialog>
#include <QFutureWatcher>

class RouteListWidget;

class RouteBrowserDialog : public QDialog {
  Q_OBJECT
public:
  RouteBrowserDialog(QWidget *parent);
  ~RouteBrowserDialog();
  QString route();

protected:
  void fetchRoutes();
  void fetchDeviceList();
  void parseDeviceList();
  void parseRouteList();

  QComboBox *device_list_;
  QComboBox *period_selector_;
  RouteListWidget *route_list_;
  QFutureWatcher<QString> device_watcher;
  QFutureWatcher<QString> route_watcher;
};
