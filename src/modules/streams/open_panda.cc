#include "open_panda.h"

#include <QApplication>
#include <QCheckBox>
#include <QDebug>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

#include "modules/system/stream_manager.h"

OpenPandaWidget::OpenPandaWidget(QWidget *parent) : AbstractStreamWidget(parent) {
  form_layout = new QFormLayout(this);
  if (dynamic_cast<PandaStream *>(StreamManager::stream()) != nullptr) {
    form_layout->addWidget(new QLabel(tr("Already connected to %1.").arg(StreamManager::stream()->routeName())));
    form_layout->addWidget(new QLabel("Close the current connection via [File menu -> Close Stream] before connecting to another Panda."));
    QTimer::singleShot(0, [this]() { emit enableOpenButton(false); });
    return;
  }

  QHBoxLayout *serial_layout = new QHBoxLayout();
  serial_layout->addWidget(serial_edit = new QComboBox());

  QPushButton *refresh = new QPushButton(tr("Refresh"));
  refresh->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  serial_layout->addWidget(refresh);
  form_layout->addRow(tr("Serial"), serial_layout);

  connect(refresh, &QPushButton::clicked, this, &OpenPandaWidget::refreshSerials);
  connect(serial_edit, &QComboBox::currentTextChanged, this, &OpenPandaWidget::buildConfigForm);

  // Populate serials
  refreshSerials();
  buildConfigForm();
}

void OpenPandaWidget::refreshSerials() {
  serial_edit->clear();
  for (auto serial : Panda::list()) {
    serial_edit->addItem(QString::fromStdString(serial));
  }
}

void OpenPandaWidget::buildConfigForm() {
  for (int i = form_layout->rowCount() - 1; i > 0; --i) {
    form_layout->removeRow(i);
  }

  QString serial = serial_edit->currentText();
  bool has_fd = false;
  bool has_panda = !serial.isEmpty();
  if (has_panda) {
    try {
      Panda panda(serial.toStdString());
      has_fd = (panda.hw_type == cereal::PandaState::PandaType::RED_PANDA) || (panda.hw_type == cereal::PandaState::PandaType::RED_PANDA_V2);
    } catch (const std::exception& e) {
      qDebug() << "failed to open panda" << serial;
      has_panda = false;
    }
  }

  if (has_panda) {
    config.serial = serial;
    config.bus_config.resize(3);
    for (int i = 0; i < config.bus_config.size(); i++) {
      QHBoxLayout *bus_layout = new QHBoxLayout;

      // CAN Speed
      bus_layout->addWidget(new QLabel(tr("CAN Speed (kbps):")));
      QComboBox *can_speed = new QComboBox;
      for (int j = 0; j < std::size(speeds); j++) {
        can_speed->addItem(QString::number(speeds[j]));

        if (data_speeds[j] == config.bus_config[i].can_speed_kbps) {
          can_speed->setCurrentIndex(j);
        }
      }
      connect(can_speed, qOverload<int>(&QComboBox::currentIndexChanged), [=](int index) {config.bus_config[i].can_speed_kbps = speeds[index];});
      bus_layout->addWidget(can_speed);

      // CAN-FD Speed
      if (has_fd) {
        QCheckBox *enable_fd = new QCheckBox("CAN-FD");
        bus_layout->addWidget(enable_fd);
        bus_layout->addWidget(new QLabel(tr("Data Speed (kbps):")));
        QComboBox *data_speed = new QComboBox;
        for (int j = 0; j < std::size(data_speeds); j++) {
          data_speed->addItem(QString::number(data_speeds[j]));

          if (data_speeds[j] == config.bus_config[i].data_speed_kbps) {
            data_speed->setCurrentIndex(j);
          }
        }

        data_speed->setEnabled(false);
        bus_layout->addWidget(data_speed);

        connect(data_speed, qOverload<int>(&QComboBox::currentIndexChanged), [=](int index) {config.bus_config[i].data_speed_kbps = data_speeds[index];});
        connect(enable_fd, &QCheckBox::stateChanged, data_speed, &QComboBox::setEnabled);
        connect(enable_fd, &QCheckBox::stateChanged, [=](int state) {config.bus_config[i].can_fd = (bool)state;});
      }

      form_layout->addRow(tr("Bus %1:").arg(i), bus_layout);
    }
  } else {
    config.serial = "";
    form_layout->addWidget(new QLabel(tr("No panda found")));
  }
}

AbstractStream *OpenPandaWidget::open() {
  try {
    return new PandaStream(qApp, config);
  } catch (std::exception &e) {
    QMessageBox::warning(nullptr, tr("Warning"), tr("Failed to connect to panda: '%1'").arg(e.what()));
    return nullptr;
  }
}
