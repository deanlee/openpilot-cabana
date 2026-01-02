#include "routes_dialog.h"

#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>

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

RoutesDialog::RoutesDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Remote routes"));

  QFormLayout* layout = new QFormLayout(this);
  layout->addRow(tr("Device"), device_list_ = new QComboBox(this));
  layout->addRow(period_selector_ = new QComboBox(this));
  layout->addRow(route_list_ = new RouteListWidget(this));
  auto button_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  layout->addRow(button_box);

  // Populate periods
  period_selector_->addItem(tr("Last week"), 7);
  period_selector_->addItem(tr("Last 2 weeks"), 14);
  period_selector_->addItem(tr("Last month"), 30);
  period_selector_->addItem(tr("Last 6 months"), 180);
  period_selector_->addItem(tr("Preserved"), -1);

  // Connect local UI changes
  connect(device_list_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RoutesDialog::fetchRoutes);
  connect(period_selector_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RoutesDialog::fetchRoutes);
  connect(route_list_, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
  connect(button_box, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);

  // Initial Fetch (Blocking)
  fetchDeviceList();
}

void RoutesDialog::fetchDeviceList() {
  device_list_->addItem(tr("Loading..."));
  QGuiApplication::setOverrideCursor(Qt::WaitCursor);

  // Using your blocking implementation
  long result = 0;
  QString json = QString::fromStdString(CommaApi2::httpGet(CommaApi2::BASE_URL + "/v1/me/devices/", &result));

  QGuiApplication::restoreOverrideCursor();

  if (!json.isEmpty()) {
    device_list_->clear();
    auto devices = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const QJsonValue& device : devices) {
      QString dongle_id = device["dongle_id"].toString();
      device_list_->addItem(dongle_id, dongle_id);
    }
  } else {
    QMessageBox::warning(this, tr("Error"), tr("Network error or Unauthorized."));
    reject();
  }
}

void RoutesDialog::fetchRoutes() {
  if (device_list_->currentIndex() == -1 || device_list_->currentData().isNull())
    return;

  route_list_->clear();
  route_list_->setEmptyText(tr("Loading..."));

  // 1. Build URL
  QString url = QString("%1/v1/devices/%2").arg(QString::fromStdString(CommaApi2::BASE_URL), device_list_->currentText());
  int period = period_selector_->currentData().toInt();
  if (period == -1) {
    url += "/routes/preserved";
  } else {
    QDateTime now = QDateTime::currentDateTime();
    url += QString("/routes_segments?start=%1&end=%2")
               .arg(now.addDays(-period).toMSecsSinceEpoch())
               .arg(now.toMSecsSinceEpoch());
  }

  // 2. Blocking Call
  QGuiApplication::setOverrideCursor(Qt::WaitCursor);
  long result = 0;
  QString json = QString::fromStdString(CommaApi2::httpGet(url.toStdString(), &result));
  QGuiApplication::restoreOverrideCursor();

  // 3. Parse immediately
  if (!json.isEmpty()) {
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
  }
  route_list_->setEmptyText(tr("No items"));
}

QString RoutesDialog::route() {
  auto current_item = route_list_->currentItem();
  return current_item ? current_item->data(Qt::UserRole).toString() : "";
}
