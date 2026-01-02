#include "open_socketcan.h"

#include <QApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>

OpenSocketCanWidget::OpenSocketCanWidget(QWidget *parent) : AbstractStreamWidget(parent) {
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->addStretch(1);

  QFormLayout *form_layout = new QFormLayout();

  QHBoxLayout *device_layout = new QHBoxLayout();
  device_edit = new QComboBox();
  device_edit->setFixedWidth(300);
  device_layout->addWidget(device_edit);

  QPushButton *refresh = new QPushButton(tr("Refresh"));
  refresh->setFixedWidth(100);
  device_layout->addWidget(refresh);
  form_layout->addRow(tr("Device"), device_layout);
  main_layout->addLayout(form_layout);

  main_layout->addStretch(1);

  connect(refresh, &QPushButton::clicked, this, &OpenSocketCanWidget::refreshDevices);
  connect(device_edit, &QComboBox::currentTextChanged, this, [=]{ config.device = device_edit->currentText(); });

  // Populate devices
  refreshDevices();
}

void OpenSocketCanWidget::refreshDevices() {
  device_edit->clear();
  for (auto device : QCanBus::instance()->availableDevices(QStringLiteral("socketcan"))) {
    device_edit->addItem(device.name());
  }
}


AbstractStream *OpenSocketCanWidget::open() {
  try {
    return new SocketCanStream(qApp, config);
  } catch (std::exception &e) {
    QMessageBox::warning(nullptr, tr("Warning"), tr("Failed to connect to SocketCAN device: '%1'").arg(e.what()));
    return nullptr;
  }
}
