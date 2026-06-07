#include "modules/streams/zmq_load_dialog.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <capnp/message.h>
#include <capnp/serialize.h>
#include <zmq.h>

#include <cstring>
#include <string>
#include <vector>

#include "cereal/gen/cpp/log.capnp.h"

static size_t fnv1a_hash_endpoint(const std::string& str) {
  const size_t fnv_prime = 0x100000001b3;
  size_t hash_value = 0xcbf29ce484222325;
  for (char c : str) {
    hash_value ^= static_cast<unsigned char>(c);
    hash_value *= fnv_prime;
  }
  return hash_value;
}

static int get_zmq_port(const std::string& endpoint) {
  const size_t hash_value = fnv1a_hash_endpoint(endpoint);
  const int start_port = 8023;
  const int max_port = 65535;
  return start_port + (hash_value % (max_port - start_port));
}

class ZmqDiagnosticWorker : public QThread {
 public:
  QString ip;
  int port;
  bool ping_ok = false;
  bool socket_ok = false;
  bool zmq_handshake_ok = false;
  bool ignition_on = false;
  bool can_query_ignition = false;

  ZmqDiagnosticWorker(QString ip, int port, QObject* parent = nullptr) : QThread(parent), ip(ip), port(port) {}

  void run() override {
    QProcess ping;
    ping.start("ping", {"-c", "1", "-W", "1", ip});
    if (ping.waitForFinished(1500)) {
      ping_ok = (ping.exitCode() == 0);
    }

    QTcpSocket tcp;
    tcp.connectToHost(ip, port);
    if (tcp.waitForConnected(1000)) {
      socket_ok = true;

      tcp.write("\xff\x00\x00\x00\x00\x00\x00\x00\x01\x7f", 10);
      if (tcp.waitForBytesWritten(500) && tcp.waitForReadyRead(1000)) {
        const QByteArray reply = tcp.read(64);
        if (reply.size() >= 10 && static_cast<unsigned char>(reply[0]) == 0xff) {
          zmq_handshake_ok = true;
        }
      }
      tcp.disconnectFromHost();

      const int panda_states_port = get_zmq_port("pandaStates");
      void* context = zmq_ctx_new();
      void* subscriber = zmq_socket(context, ZMQ_SUB);
      zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, "", 0);
      int timeout = 2500;
      zmq_setsockopt(subscriber, ZMQ_RCVTIMEO, &timeout, sizeof(int));

      const std::string addr = "tcp://" + ip.toStdString() + ":" + std::to_string(panda_states_port);
      if (zmq_connect(subscriber, addr.c_str()) == 0) {
        QThread::msleep(500);
        zmq_msg_t reply_msg;
        zmq_msg_init(&reply_msg);
        const int rc = zmq_msg_recv(&reply_msg, subscriber, 0);
        if (rc > 0) {
          try {
            const int size = zmq_msg_size(&reply_msg);
            if (size >= 8) {
              const int words_size = size / static_cast<int>(sizeof(capnp::word));
              std::vector<capnp::word> aligned_buf(words_size);
              memcpy(aligned_buf.data(), zmq_msg_data(&reply_msg), size);

              capnp::FlatArrayMessageReader reader(
                  kj::ArrayPtr<const capnp::word>(aligned_buf.data(), words_size));
              auto event = reader.getRoot<cereal::Event>();
              if (event.which() == cereal::Event::Which::PANDA_STATES) {
                can_query_ignition = true;
                for (auto p : event.getPandaStates()) {
                  if (p.getIgnitionLine() || p.getIgnitionCan()) {
                    ignition_on = true;
                  }
                }
              }
            }
          } catch (...) {
          }
        }
        zmq_msg_close(&reply_msg);
      }
      zmq_close(subscriber);
      zmq_ctx_destroy(context);
    }
  }
};

ZmqLoadDialog::ZmqLoadDialog(const QString& ip_address, QWidget* parent) : QDialog(parent), ip_(ip_address) {
  setWindowTitle(tr("Connecting to ZMQ Stream"));
  setWindowModality(Qt::WindowModal);
  setFixedSize(420, 310);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(20, 20, 20, 20);
  layout->setSpacing(12);

  title_label_ = new QLabel(tr("Connecting to %1...").arg(ip_.isEmpty() ? "127.0.0.1" : ip_), this);
  title_label_->setStyleSheet("font-size: 15px; font-weight: bold;");
  layout->addWidget(title_label_);

  auto* progress = new QProgressBar(this);
  progress->setRange(0, 0);
  progress->setFixedHeight(12);
  layout->addWidget(progress);

  ping_label_ = new QLabel("⚪ [Ping] -> Connecting...", this);
  socket_label_ = new QLabel("⚪ [Socket] -> Connecting...", this);
  zmq_label_ = new QLabel("⚪ [ZMQ Handshake] -> Connecting...", this);
  ignition_label_ = new QLabel("⚪ [Ignition] -> Connecting...", this);

  ping_label_->setStyleSheet("font-size: 13px; color: #9ca3af;");
  socket_label_->setStyleSheet("font-size: 13px; color: #9ca3af;");
  zmq_label_->setStyleSheet("font-size: 13px; color: #9ca3af;");
  ignition_label_->setStyleSheet("font-size: 13px; color: #9ca3af;");

  layout->addWidget(ping_label_);
  layout->addWidget(socket_label_);
  layout->addWidget(zmq_label_);
  layout->addWidget(ignition_label_);

  auto* line = new QFrame(this);
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Sunken);
  layout->addWidget(line);

  advice_label_ = new QLabel(tr("Initializing connection diagnostics..."), this);
  advice_label_->setWordWrap(true);
  advice_label_->setStyleSheet("font-size: 12px; color: #6b7280;");
  layout->addWidget(advice_label_);

  layout->addStretch();

  auto* btn_layout = new QHBoxLayout();
  auto* abort_btn = new QPushButton(tr("&Abort"), this);
  abort_btn->setFixedWidth(100);
  btn_layout->addStretch();
  btn_layout->addWidget(abort_btn);
  layout->addLayout(btn_layout);

  connect(abort_btn, &QPushButton::clicked, this, &QDialog::reject);

  diag_timer_ = new QTimer(this);
  connect(diag_timer_, &QTimer::timeout, this, &ZmqLoadDialog::runDiagnostics);
  diag_timer_->start(5000);

  QTimer::singleShot(200, this, &ZmqLoadDialog::runDiagnostics);
}

ZmqLoadDialog::~ZmqLoadDialog() {
  diag_timer_->stop();
  if (worker_ && worker_->isRunning()) {
    worker_->requestInterruption();
    worker_->wait();
  }
}

void ZmqLoadDialog::runDiagnostics() {
  if (worker_ && worker_->isRunning()) {
    return;
  }

  const int port = get_zmq_port("can");
  worker_ = new ZmqDiagnosticWorker(ip_.isEmpty() ? "127.0.0.1" : ip_, port, this);
  connect(worker_, &QThread::finished, this, [this]() {
    updateUI();
    worker_->deleteLater();
    worker_ = nullptr;
  });
  worker_->start();
}

void ZmqLoadDialog::updateUI() {
  if (!worker_) return;

  if (worker_->ping_ok) {
    ping_label_->setText(tr("🟢 [Ping] -> OK (Device is online)"));
    ping_label_->setStyleSheet("color: #22c55e; font-size: 13px; font-weight: bold;");
  } else {
    ping_label_->setText(tr("🔴 [Ping] -> KO (Device unreachable)"));
    ping_label_->setStyleSheet("color: #ef4444; font-size: 13px; font-weight: bold;");
  }

  if (worker_->socket_ok) {
    socket_label_->setText(tr("🟢 [Socket] -> OK (Port %1 is open)").arg(worker_->port));
    socket_label_->setStyleSheet("color: #22c55e; font-size: 13px; font-weight: bold;");

    if (worker_->zmq_handshake_ok) {
      zmq_label_->setText(tr("🟢 [ZMQ Status] -> OK (ZMQ handshake succeeded)"));
      zmq_label_->setStyleSheet("color: #22c55e; font-size: 13px; font-weight: bold;");
    } else {
      zmq_label_->setText(tr("🔴 [ZMQ Status] -> KO (Handshake failed)"));
      zmq_label_->setStyleSheet("color: #ef4444; font-size: 13px; font-weight: bold;");
    }
  } else {
    socket_label_->setText(tr("🔴 [Socket] -> KO (Port %1 is closed)").arg(worker_->port));
    socket_label_->setStyleSheet("color: #ef4444; font-size: 13px; font-weight: bold;");

    zmq_label_->setText(tr("⚪ [ZMQ Status] -> --"));
    zmq_label_->setStyleSheet("color: #9ca3af; font-size: 13px;");
  }

  if (!worker_->ping_ok) {
    ignition_label_->setText(tr("⚪ [Ignition] -> --"));
    ignition_label_->setStyleSheet("color: #9ca3af; font-size: 13px;");
    if (ip_ == "127.0.0.1" || ip_ == "localhost") {
      advice_label_->setText(tr("Make sure openpilot/camerad is running locally on this PC."));
    } else {
      advice_label_->setText(
          tr("Make sure the Comma device is powered ON and your PC is connected to the same Wi-Fi subnet."));
    }
  } else if (!worker_->socket_ok) {
    ignition_label_->setText(tr("⚪ [Ignition] -> --"));
    ignition_label_->setStyleSheet("color: #9ca3af; font-size: 13px;");
    advice_label_->setText(
        tr("The device is reachable, but the ZMQ bridge service is not running. Please start the bridge process "
           "on the device."));
  } else {
    if (worker_->can_query_ignition) {
      if (worker_->ignition_on) {
        ignition_label_->setText(tr("🟢 [Ignition] -> ON (CAN traffic active)"));
        ignition_label_->setStyleSheet("color: #22c55e; font-size: 13px; font-weight: bold;");
        advice_label_->setText(tr("Initializing live CAN stream... Waiting for the first packets to arrive."));
      } else {
        ignition_label_->setText(tr("🟡 [Ignition] -> OFF (Panda in standby)"));
        ignition_label_->setStyleSheet("color: #f59e0b; font-size: 13px; font-weight: bold;");
        advice_label_->setText(
            tr("Connection established! Please turn your car's IGNITION ON to start streaming live CAN data."));
      }
    } else {
      ignition_label_->setText(tr("🔴 [Ignition] -> KO (Cannot query pandaStates)"));
      ignition_label_->setStyleSheet("color: #ef4444; font-size: 13px; font-weight: bold;");
      advice_label_->setText(tr("Port is open, but cannot query pandaStates. Check your firewall settings."));
    }
  }
}
