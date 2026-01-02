#pragma once

#include <QComboBox>
#include <QDialog>

class RouteListWidget;

class RoutesDialog : public QDialog {
  Q_OBJECT
public:
  RoutesDialog(QWidget *parent);
  QString route();

protected:
  void fetchRoutes();
  void fetchDeviceList();

  QComboBox *device_list_;
  QComboBox *period_selector_;
  RouteListWidget *route_list_;
};
