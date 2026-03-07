#include "socketcan.h"

#include <QApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>

SocketCanWidget::SocketCanWidget(QWidget* parent) : AbstractStreamWidget(parent) {
  QVBoxLayout* main_layout = new QVBoxLayout(this);
  main_layout->addStretch(1);

  QFormLayout* form_layout = new QFormLayout();

  QHBoxLayout* device_layout = new QHBoxLayout();
  device_edit = new QComboBox();
  device_layout->addWidget(device_edit, 1);

  QPushButton* refresh = new QPushButton(tr("Refresh"));
  device_layout->addWidget(refresh);
  form_layout->addRow(tr("Device"), device_layout);
  main_layout->addLayout(form_layout);

  main_layout->addStretch(1);
  setFocusProxy(device_edit);

  connect(refresh, &QPushButton::clicked, this, &SocketCanWidget::refreshDevices);
  connect(device_edit, &QComboBox::currentTextChanged, this, [this](const QString& text) { config.device = text; });

  refreshDevices();
}

void SocketCanWidget::refreshDevices() {
  device_edit->clear();
  for (auto& device : QCanBus::instance()->availableDevices(QStringLiteral("socketcan"))) {
    device_edit->addItem(device.name());
  }
  config.device = device_edit->currentText();
  emit enableOpenButton(!config.device.isEmpty());
}

AbstractStream* SocketCanWidget::open() {
  try {
    return new SocketCanStream(qApp, config);
  } catch (std::exception& e) {
    QMessageBox::warning(nullptr, tr("Warning"), tr("Failed to connect to SocketCAN device: '%1'").arg(e.what()));
    return nullptr;
  }
}
