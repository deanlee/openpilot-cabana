#include "route_browser.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QtConcurrent>

#include "replay/include/api.h"

// The RouteListWidget class extends QListWidget to display a custom message when empty
class RouteListWidget : public QListWidget {
 public:
  RouteListWidget(QWidget* parent = nullptr) : QListWidget(parent) {}
  void setEmptyText(const QString& text) {
    empty_text_ = text;
    viewport()->update();
  }
  void paintEvent(QPaintEvent* event) override {
    QListWidget::paintEvent(event);
    if (count() == 0) {
      QPainter painter(viewport());
      painter.drawText(viewport()->rect(), Qt::AlignCenter, empty_text_);
    }
  }
  QString empty_text_ = tr("No items");
};

// --- RouteBrowserDialog Implementation ---
RouteBrowserDialog::RouteBrowserDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Remote routes"));
  setMinimumWidth(400);

  QFormLayout* layout = new QFormLayout(this);
  layout->addRow(tr("Device"), device_list_ = new QComboBox(this));
  layout->addRow(period_selector_ = new QComboBox(this));
  layout->addRow(route_list_ = new RouteListWidget(this));
  auto button_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  layout->addRow(button_box);

  period_selector_->addItem(tr("Last week"), 7);
  period_selector_->addItem(tr("Last 2 weeks"), 14);
  period_selector_->addItem(tr("Last month"), 30);
  period_selector_->addItem(tr("Last 6 months"), 180);
  period_selector_->addItem(tr("Preserved"), -1);

  // Connect Watchers
  connect(&device_watcher, &QFutureWatcher<QString>::finished, this, &RouteBrowserDialog::parseDeviceList);
  connect(&route_watcher, &QFutureWatcher<QString>::finished, this, &RouteBrowserDialog::parseRouteList);

  // UI Trigger connections
  connect(device_list_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RouteBrowserDialog::fetchRoutes);
  connect(period_selector_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RouteBrowserDialog::fetchRoutes);
  connect(button_box, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);

  fetchDeviceList();
}

RouteBrowserDialog::~RouteBrowserDialog() {
  device_watcher.disconnect();
  route_watcher.disconnect();
  device_watcher.cancel();
  route_watcher.cancel();
}

void RouteBrowserDialog::fetchDeviceList() {
  device_list_->clear();
  device_list_->addItem(tr("Loading devices..."));

  // Run existing libcurl function in background
  auto future = QtConcurrent::run([]() {
    long result = 0;
    std::string url = CommaApi2::BASE_URL + "/v1/me/devices/";
    return QString::fromStdString(CommaApi2::httpGet(url, &result));
  });
  device_watcher.setFuture(future);
}

void RouteBrowserDialog::parseDeviceList() {
  QString json = device_watcher.result();
  device_list_->clear();

  if (json.isEmpty()) {
    QMessageBox::warning(this, tr("Error"), tr("Failed to load devices (Auth error?)"));
    return;
  }

  auto devices = QJsonDocument::fromJson(json.toUtf8()).array();
  for (const QJsonValue& device : devices) {
    QString id = device["dongle_id"].toString();
    device_list_->addItem(id, id);
  }
}

void RouteBrowserDialog::fetchRoutes() {
  if (device_list_->currentIndex() == -1 || device_list_->currentData().isNull()) return;

  route_list_->clear();
  route_list_->setEmptyText(tr("Loading..."));

  QString url = QString::fromStdString(CommaApi2::BASE_URL) + "/v1/devices/" + device_list_->currentText();
  int period = period_selector_->currentData().toInt();
  if (period == -1) {
    url += "/routes/preserved";
  } else {
    QDateTime now = QDateTime::currentDateTime();
    url += QString("/routes_segments?start=%1&end=%2")
               .arg(now.addDays(-period).toMSecsSinceEpoch())
               .arg(now.toMSecsSinceEpoch());
  }

  // Run existing libcurl function in background
  auto future = QtConcurrent::run([url]() {
    long result = 0;
    return QString::fromStdString(CommaApi2::httpGet(url.toStdString(), &result));
  });
  route_watcher.setFuture(future);
}

void RouteBrowserDialog::parseRouteList() {
  QString json = route_watcher.result();
  if (json.isEmpty()) {
    route_list_->setEmptyText(tr("No routes found or network error."));
    return;
  }

  int period = period_selector_->currentData().toInt();
  auto routes = QJsonDocument::fromJson(json.toUtf8()).array();

  for (const QJsonValue& route : routes) {
    QDateTime from, to;
    if (period == -1) {
      from = QDateTime::fromString(route["start_time"].toString(), Qt::ISODateWithMs);
      to = QDateTime::fromString(route["end_time"].toString(), Qt::ISODateWithMs);
    } else {
      from = QDateTime::fromMSecsSinceEpoch(route["start_time_utc_millis"].toDouble());
      to = QDateTime::fromMSecsSinceEpoch(route["end_time_utc_millis"].toDouble());
    }
    auto item = new QListWidgetItem(QString("%1    %2min").arg(from.toString()).arg(from.secsTo(to) / 60));
    item->setData(Qt::UserRole, route["fullname"].toString());
    route_list_->addItem(item);
  }

  if (route_list_->count() > 0) route_list_->setCurrentRow(0);
  route_list_->setEmptyText(tr("No items"));
}

QString RouteBrowserDialog::route() {
  auto item = route_list_->currentItem();
  return item ? item->data(Qt::UserRole).toString() : "";
}
