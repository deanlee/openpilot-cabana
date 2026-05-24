#include "mainwin.h"

#include <QApplication>
#include <QDialog>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUndoView>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <zmq.h>
#include <capnp/message.h>
#include <capnp/serialize.h>

#include "cereal/gen/cpp/log.capnp.h"
#include "core/streams/device_stream.h"
#include "core/commands/commands.h"
#include "modules/dbc/dbc_controller.h"
#include "modules/dbc/export.h"
#include "modules/settings/settings_dialog.h"
#include "modules/streams/stream_selector.h"
#include "modules/system/stream_manager.h"
#include "modules/system/system_relay.h"
#include "replay/include/http.h"
#include "tools/findsignal.h"
#include "widgets/guide_overlay.h"

MainWindow::MainWindow(AbstractStream* stream, const QString& dbc_file) : QMainWindow() {
  dbc_controller_ = new DbcController(this);
  charts_panel = new ChartsPanel(this);
  inspector_widget_ = new MessageInspector(charts_panel, this);
  setCentralWidget(inspector_widget_);

  status_bar_ = new StatusBar(this);
  setStatusBar(status_bar_);
  setupDocks();
  setupMenus();
  createShortcuts();

  // save default window state to allow resetting it
  default_window_state_ = saveState();

  // restore states
  if (!settings.geometry.isEmpty()) restoreGeometry(settings.geometry);
  if (!settings.window_state.isEmpty()) restoreState(settings.window_state);
  if (isMaximized()) setGeometry(screen()->availableGeometry());

  setupConnections();

  QTimer::singleShot(0, this, [=]() { stream ? openStream(stream, dbc_file) : selectAndOpenStream(); });
  show();
}

void MainWindow::setupConnections() {
  auto& relay = SystemRelay::instance();
  relay.installGlobalHandlers();

  connect(&relay, &SystemRelay::logMessage, status_bar_, &StatusBar::showMessage);
  connect(&relay, &SystemRelay::downloadProgress, status_bar_, &StatusBar::updateDownloadProgress);
  connect(&settings, &Settings::changed, status_bar_, &StatusBar::updateMetrics);
  connect(GetDBC(), &dbc::Manager::DBCFileChanged, this, &MainWindow::DBCFileChanged);
  connect(UndoStack::instance(), &QUndoStack::cleanChanged, this, &MainWindow::undoStackCleanChanged);
  connect(&StreamManager::instance(), &StreamManager::streamChanged, this, &MainWindow::onStreamChanged);
  connect(&StreamManager::instance(), &StreamManager::eventsMerged, this, &MainWindow::eventsMerged);

  connect(charts_panel, &ChartsPanel::openMessage, message_list_, &MessageList::selectMessage);
  connect(inspector_widget_->getMessageView(), &MessageView::activeMessageChanged, message_list_,
          &MessageList::selectMessage);
}

void MainWindow::setupMenus() {
  createFileMenu();
  createEditMenu();
  createViewMenu();
  createToolsMenu();
  createHelpMenu();
}

void MainWindow::createFileMenu() {
  QMenu* file_menu = menuBar()->addMenu(tr("&File"));
  file_menu->addAction(tr("Open Stream..."), this, &MainWindow::selectAndOpenStream);
  close_stream_act_ = file_menu->addAction(tr("Close stream"), this, &MainWindow::closeStream);
  export_to_csv_act_ = file_menu->addAction(tr("Export to CSV..."), this, &MainWindow::exportToCSV);
  close_stream_act_->setEnabled(false);
  export_to_csv_act_->setEnabled(false);
  file_menu->addSeparator();

  file_menu->addAction(tr("New DBC File"), [this]() { dbc_controller_->newFile(); }, QKeySequence::New);
  file_menu->addAction(tr("Open DBC File..."), [this]() { dbc_controller_->openFile(); }, QKeySequence::Open);

  manage_dbcs_menu_ = file_menu->addMenu(tr("Manage &DBC Files"));
  connect(manage_dbcs_menu_, &QMenu::aboutToShow, this,
          [this]() { dbc_controller_->populateManageMenu(manage_dbcs_menu_); });

  recent_files_menu_ = file_menu->addMenu(tr("Open &Recent"));
  connect(recent_files_menu_, &QMenu::aboutToShow, this,
          [this]() { dbc_controller_->populateRecentMenu(recent_files_menu_); });

  file_menu->addSeparator();
  QMenu* load_opendbc_menu = file_menu->addMenu(tr("Load DBC from commaai/opendbc"));
  dbc_controller_->populateOpendbcFiles(load_opendbc_menu);

  file_menu->addAction(tr("Load DBC From Clipboard"), [=]() { dbc_controller_->loadFromClipboard(); });

  file_menu->addSeparator();
  save_dbc_ = file_menu->addAction(tr("Save DBC..."), dbc_controller_, &DbcController::save, QKeySequence::Save);
  save_dbc_as_ =
      file_menu->addAction(tr("Save DBC As..."), dbc_controller_, &DbcController::saveAs, QKeySequence::SaveAs);
  copy_dbc_to_clipboard_ =
      file_menu->addAction(tr("Copy DBC To Clipboard"), dbc_controller_, &DbcController::saveToClipboard);

  file_menu->addSeparator();
  file_menu->addAction(tr("Settings..."), this, &MainWindow::setOption, QKeySequence::Preferences);

  file_menu->addSeparator();
  file_menu->addAction(tr("E&xit"), qApp, &QApplication::closeAllWindows, QKeySequence::Quit);
}

void MainWindow::createEditMenu() {
  QMenu* edit_menu = menuBar()->addMenu(tr("&Edit"));
  auto undo_act = UndoStack::instance()->createUndoAction(this, tr("&Undo"));
  undo_act->setShortcuts(QKeySequence::Undo);
  edit_menu->addAction(undo_act);
  auto redo_act = UndoStack::instance()->createRedoAction(this, tr("&Redo"));
  redo_act->setShortcuts(QKeySequence::Redo);
  edit_menu->addAction(redo_act);
  edit_menu->addSeparator();

  QMenu* commands_menu = edit_menu->addMenu(tr("Command &List"));
  QWidgetAction* commands_act = new QWidgetAction(this);
  QUndoView* view = new QUndoView(UndoStack::instance(), this);  // Parent set here
  view->setEmptyLabel(tr("No commands"));
  commands_act->setDefaultWidget(view);
  commands_menu->addAction(commands_act);
}

void MainWindow::createViewMenu() {
  QMenu* view_menu = menuBar()->addMenu(tr("&View"));
  auto act = view_menu->addAction(tr("Full Screen"), this, &MainWindow::toggleFullScreen, QKeySequence::FullScreen);
  addAction(act);
  view_menu->addSeparator();
  view_menu->addAction(messages_dock_->toggleViewAction());
  view_menu->addAction(video_dock_->toggleViewAction());
  view_menu->addSeparator();
  view_menu->addAction(tr("Reset Window Layout"), [this]() { restoreState(default_window_state_); });
}

void MainWindow::createToolsMenu() {
  tools_menu_ = menuBar()->addMenu(tr("&Tools"));
  tools_menu_->addAction(tr("Find &Similar Bits"), this, &MainWindow::findSimilarBits);
  tools_menu_->addAction(tr("&Find Signal"), this, &MainWindow::findSignal);
}

void MainWindow::createHelpMenu() {
  QMenu* help_menu = menuBar()->addMenu(tr("&Help"));
  help_menu->addAction(tr("Help"), this, &MainWindow::onlineHelp, QKeySequence::HelpContents);
  help_menu->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

void MainWindow::setupDocks() {
  createMessagesDock();
  createVideoChartsDock();
}

void MainWindow::createMessagesDock() {
  messages_dock_ = new QDockWidget(tr("MESSAGES"), this);
  messages_dock_->setObjectName("MessagesPanel");
  messages_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::TopDockWidgetArea |
                                  Qt::BottomDockWidgetArea);
  messages_dock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                              QDockWidget::DockWidgetClosable);

  message_list_ = new MessageList(this);
  messages_dock_->setWidget(message_list_);
  addDockWidget(Qt::LeftDockWidgetArea, messages_dock_);

  connect(message_list_, &MessageList::titleChanged, messages_dock_, &QDockWidget::setWindowTitle);
  connect(message_list_, &MessageList::msgSelectionChanged, inspector_widget_, &MessageInspector::setMessage);
}

void MainWindow::createVideoChartsDock() {
  video_dock_ = new QDockWidget("", this);
  video_dock_->setObjectName("VideoPanel");
  video_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  video_dock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                           QDockWidget::DockWidgetClosable);

  QWidget* charts_container = new QWidget(this);
  charts_layout_ = new QVBoxLayout(charts_container);
  charts_layout_->setContentsMargins(0, 0, 0, 0);
  charts_layout_->addWidget(charts_panel);

  // splitter between video and charts
  video_splitter_ = new PanelSplitter(Qt::Vertical, this);
  video_player_ = new VideoPlayer(this);
  video_splitter_->addWidget(video_player_);

  video_splitter_->addWidget(charts_container);
  video_splitter_->setStretchFactor(0, 0);
  video_splitter_->setStretchFactor(1, 1);

  video_dock_->setWidget(video_splitter_);
  addDockWidget(Qt::RightDockWidgetArea, video_dock_);

  connect(charts_panel, &ChartsPanel::toggleChartsDocking, this, &MainWindow::toggleChartsDocking);
  connect(charts_panel, &ChartsPanel::showCursor, video_player_, &VideoPlayer::showThumbnail);
}

void MainWindow::createShortcuts() {
  auto shortcut = new QShortcut(QKeySequence(Qt::Key_Space), this, nullptr, nullptr, Qt::ApplicationShortcut);
  connect(shortcut, &QShortcut::activated, this, []() {
    if (auto* stream = StreamManager::stream()) {
      stream->pause(!stream->isPaused());
    }
  });
  // TODO: add more shortcuts here.
}

void MainWindow::onStreamChanged() {
  if (auto* handle = video_splitter_->handle(1)) {
    handle->setEnabled(!StreamManager::instance().isLiveStream());
  }
}

void MainWindow::undoStackCleanChanged(bool clean) { setWindowModified(!clean); }

void MainWindow::DBCFileChanged() {
  UndoStack::instance()->clear();

  // Update file menu
  int cnt = GetDBC()->nonEmptyFileCount();
  save_dbc_->setText(cnt > 1 ? tr("Save %1 DBCs...").arg(cnt) : tr("Save DBC..."));
  save_dbc_->setEnabled(cnt > 0);
  save_dbc_as_->setEnabled(cnt == 1);
  // TODO: Support clipboard for multiple files
  copy_dbc_to_clipboard_->setEnabled(cnt == 1);
  manage_dbcs_menu_->setEnabled(StreamManager::instance().hasStream());

  QStringList title;
  for (const auto &f : GetDBC()->allFiles()) {
    title.push_back(tr("(%1) %2").arg(toString(GetDBC()->getSourcesForFile(f.get())), f->name()));
  }
  setWindowFilePath(title.join(" | "));

  QTimer::singleShot(20, this, &::MainWindow::restoreSessionState);
}

void MainWindow::selectAndOpenStream() {
  StreamSelector dlg(this);
  if (dlg.exec()) {
    openStream(dlg.stream(), dlg.dbcFile());
  }
}

void MainWindow::closeStream() {
  openStream(new DummyStream(this));
  if (GetDBC()->nonEmptyFileCount() > 0) {
    emit GetDBC()->DBCFileChanged();
  }
  statusBar()->showMessage(tr("stream closed"));
}

void MainWindow::exportToCSV() {
  auto* stream = StreamManager::stream();
  if (!stream) return;

  QString dir = QString("%1/%2.csv").arg(settings.last_dir).arg(stream->routeName());
  QString fn = QFileDialog::getSaveFileName(this, "Export stream to CSV file", dir, tr("csv (*.csv)"));
  if (!fn.isEmpty()) {
    exportMessagesToCSV(fn);
    QMessageBox::information(this, tr("Export"), tr("Data successfully exported to:\n%1").arg(fn));
  }
}

void MainWindow::openStream(AbstractStream* stream, const QString& dbc_file) {
  auto& sm = StreamManager::instance();
  sm.setStream(stream, dbc_file);

  inspector_widget_->clear();
  dbc_controller_->loadFile(dbc_file);

  bool has_stream = sm.hasStream();
  bool is_live_stream = sm.isLiveStream();

  close_stream_act_->setEnabled(has_stream);
  export_to_csv_act_->setEnabled(has_stream);
  tools_menu_->setEnabled(has_stream);

  video_dock_->setWindowTitle(has_stream ? sm.stream()->routeName() : tr("Video"));
  if (is_live_stream || video_splitter_->sizes()[0] == 0) {
    // display video at minimum size.
    video_splitter_->setSizes({1, 1});
  }
  // Don't overwrite already loaded DBC
  if (!GetDBC()->nonEmptyFileCount()) {
    dbc_controller_->newFile();
  }

  if (has_stream) {
    createLoadingDialog(is_live_stream);
  }
}

static size_t fnv1a_hash_endpoint(const std::string &str) {
  const size_t fnv_prime = 0x100000001b3;
  size_t hash_value = 0xcbf29ce484222325;
  for (char c : str) {
    hash_value ^= (unsigned char)c;
    hash_value *= fnv_prime;
  }
  return hash_value;
}

static int get_zmq_port(std::string endpoint) {
  size_t hash_value = fnv1a_hash_endpoint(endpoint);
  int start_port = 8023;
  int max_port = 65535;
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

  ZmqDiagnosticWorker(QString ip, int port, QObject *parent = nullptr) 
    : QThread(parent), ip(ip), port(port) {}

  void run() override {
    // 1. Ping Check
    QProcess ping;
    ping.start("ping", {"-c", "1", "-W", "1", ip});
    if (ping.waitForFinished(1500)) {
      ping_ok = (ping.exitCode() == 0);
    }

    // 2. Socket Check
    QTcpSocket tcp;
    tcp.connectToHost(ip, port);
    if (tcp.waitForConnected(1000)) {
      socket_ok = true;
      
      // 3. Additional ZMQ protocol handshake check
      tcp.write("\xff\x00\x00\x00\x00\x00\x00\x00\x01\x7f", 10);
      if (tcp.waitForBytesWritten(500) && tcp.waitForReadyRead(1000)) {
        QByteArray reply = tcp.read(64);
        if (reply.size() >= 10 && (unsigned char)reply[0] == 0xff) {
          zmq_handshake_ok = true;
        }
      }
      tcp.disconnectFromHost();
      
      // 4. Ignition Check
      int panda_states_port = get_zmq_port("pandaStates");
      void *context = zmq_ctx_new();
      void *subscriber = zmq_socket(context, ZMQ_SUB);
      zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, "", 0);
      int timeout = 2500; // Increase to 2.5 seconds timeout
      zmq_setsockopt(subscriber, ZMQ_RCVTIMEO, &timeout, sizeof(int));
      
      std::string addr = "tcp://" + ip.toStdString() + ":" + std::to_string(panda_states_port);
      if (zmq_connect(subscriber, addr.c_str()) == 0) {
        QThread::msleep(500); // Allow ZMQ background connection handshake to complete
        zmq_msg_t reply_msg;
        zmq_msg_init(&reply_msg);
        int rc = zmq_msg_recv(&reply_msg, subscriber, 0);
        if (rc > 0) {
          try {
            int size = zmq_msg_size(&reply_msg);
            if (size >= 8) {
              int words_size = size / sizeof(capnp::word);
              std::vector<capnp::word> aligned_buf(words_size);
              memcpy(aligned_buf.data(), zmq_msg_data(&reply_msg), size);
              
              capnp::FlatArrayMessageReader reader(
                kj::ArrayPtr<const capnp::word>(aligned_buf.data(), words_size)
              );
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
            // Safe fallback on parsing error
          }
        }
        zmq_msg_close(&reply_msg);
      }
      zmq_close(subscriber);
      zmq_ctx_destroy(context);
    }
  }
};

class ZmqLoadDialog : public QDialog {
private:
  QString ip;
  QLabel *title_label;
  QLabel *ping_label;
  QLabel *socket_label;
  QLabel *zmq_label;
  QLabel *ignition_label;
  QLabel *advice_label;
  QTimer *diag_timer;
  QPointer<ZmqDiagnosticWorker> worker = nullptr;

public:
  ZmqLoadDialog(QString ip_address, QWidget *parent = nullptr) : QDialog(parent), ip(ip_address) {
    setWindowTitle(tr("Connecting to ZMQ Stream"));
    setWindowModality(Qt::WindowModal);
    setFixedSize(420, 310);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    title_label = new QLabel(tr("Connecting to %1...").arg(ip.isEmpty() ? "127.0.0.1" : ip), this);
    title_label->setStyleSheet("font-size: 15px; font-weight: bold;");
    layout->addWidget(title_label);

    QProgressBar *progress = new QProgressBar(this);
    progress->setRange(0, 0);
    progress->setFixedHeight(12);
    layout->addWidget(progress);

    ping_label = new QLabel("⚪ [Ping] ➔ Connecting...", this);
    socket_label = new QLabel("⚪ [Socket] ➔ Connecting...", this);
    zmq_label = new QLabel("⚪ [ZMQ Handshake] ➔ Connecting...", this);
    ignition_label = new QLabel("⚪ [Ignition] ➔ Connecting...", this);

    ping_label->setStyleSheet("font-size: 13px; color: #9ca3af;");
    socket_label->setStyleSheet("font-size: 13px; color: #9ca3af;");
    zmq_label->setStyleSheet("font-size: 13px; color: #9ca3af;");
    ignition_label->setStyleSheet("font-size: 13px; color: #9ca3af;");

    layout->addWidget(ping_label);
    layout->addWidget(socket_label);
    layout->addWidget(zmq_label);
    layout->addWidget(ignition_label);

    QFrame *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    advice_label = new QLabel(tr("Initializing connection diagnostics..."), this);
    advice_label->setWordWrap(true);
    advice_label->setStyleSheet("font-size: 12px; color: #6b7280;");
    layout->addWidget(advice_label);

    layout->addStretch();

    QHBoxLayout *btn_layout = new QHBoxLayout();
    QPushButton *abort_btn = new QPushButton(tr("&Abort"), this);
    abort_btn->setFixedWidth(100);
    btn_layout->addStretch();
    btn_layout->addWidget(abort_btn);
    layout->addLayout(btn_layout);

    connect(abort_btn, &QPushButton::clicked, this, &QDialog::reject);

    diag_timer = new QTimer(this);
    connect(diag_timer, &QTimer::timeout, this, &ZmqLoadDialog::runDiagnostics);
    diag_timer->start(5000);

    QTimer::singleShot(200, this, &ZmqLoadDialog::runDiagnostics);
  }

  ~ZmqLoadDialog() {
    diag_timer->stop();
    if (worker && worker->isRunning()) {
      worker->requestInterruption();
      worker->wait();
    }
  }

  void runDiagnostics() {
    if (worker && worker->isRunning()) {
      return;
    }
    
    int port = get_zmq_port("can");
    worker = new ZmqDiagnosticWorker(ip.isEmpty() ? "127.0.0.1" : ip, port, this);
    connect(worker, &QThread::finished, this, [=]() {
      updateUI();
      worker->deleteLater();
      worker = nullptr;
    });
    worker->start();
  }

  void updateUI() {
    if (!worker) return;

    if (worker->ping_ok) {
      ping_label->setText(tr("🟢 [Ping] ➔ OK (Device is online)"));
      ping_label->setStyleSheet("color: #22c55e; font-size: 13px; font-weight: bold;");
    } else {
      ping_label->setText(tr("🔴 [Ping] ➔ KO (Device unreachable)"));
      ping_label->setStyleSheet("color: #ef4444; font-size: 13px; font-weight: bold;");
    }

    if (worker->socket_ok) {
      socket_label->setText(tr("🟢 [Socket] ➔ OK (Port %1 is open)").arg(worker->port));
      socket_label->setStyleSheet("color: #22c55e; font-size: 13px; font-weight: bold;");
      
      if (worker->zmq_handshake_ok) {
        zmq_label->setText(tr("🟢 [ZMQ Status] ➔ OK (ZMQ handshake succeeded)"));
        zmq_label->setStyleSheet("color: #22c55e; font-size: 13px; font-weight: bold;");
      } else {
        zmq_label->setText(tr("🔴 [ZMQ Status] ➔ KO (Handshake failed)"));
        zmq_label->setStyleSheet("color: #ef4444; font-size: 13px; font-weight: bold;");
      }
    } else {
      socket_label->setText(tr("🔴 [Socket] ➔ KO (Port %1 is closed)").arg(worker->port));
      socket_label->setStyleSheet("color: #ef4444; font-size: 13px; font-weight: bold;");
      
      zmq_label->setText(tr("⚪ [ZMQ Status] ➔ --"));
      zmq_label->setStyleSheet("color: #9ca3af; font-size: 13px;");
    }

    if (!worker->ping_ok) {
      ignition_label->setText(tr("⚪ [Ignition] ➔ --"));
      ignition_label->setStyleSheet("color: #9ca3af; font-size: 13px;");
      if (ip == "127.0.0.1" || ip == "localhost") {
        advice_label->setText(tr("Make sure openpilot/camerad is running locally on this PC."));
      } else {
        advice_label->setText(tr("Make sure the Comma device is powered ON and your PC is connected to the same Wi-Fi subnet."));
      }
    } else if (!worker->socket_ok) {
      ignition_label->setText(tr("⚪ [Ignition] ➔ --"));
      ignition_label->setStyleSheet("color: #9ca3af; font-size: 13px;");
      advice_label->setText(tr("The device is reachable, but the ZMQ bridge service is not running. Please start the bridge process on the device."));
    } else {
      if (worker->can_query_ignition) {
        if (worker->ignition_on) {
          ignition_label->setText(tr("🟢 [Ignition] ➔ ON (CAN traffic active)"));
          ignition_label->setStyleSheet("color: #22c55e; font-size: 13px; font-weight: bold;");
          advice_label->setText(tr("Initializing live CAN stream... Waiting for the first packets to arrive."));
        } else {
          ignition_label->setText(tr("🟡 [Ignition] ➔ OFF (Panda in standby)"));
          ignition_label->setStyleSheet("color: #f59e0b; font-size: 13px; font-weight: bold;");
          advice_label->setText(tr("Connection established! Please turn your car's IGNITION ON to start streaming live CAN data."));
        }
      } else {
        ignition_label->setText(tr("🔴 [Ignition] ➔ KO (Cannot query pandaStates)"));
        ignition_label->setStyleSheet("color: #ef4444; font-size: 13px; font-weight: bold;");
        advice_label->setText(tr("Port is open, but cannot query pandaStates. Check your firewall settings."));
      }
    }
  }
};

void MainWindow::createLoadingDialog(bool is_live) {
  auto* stream = StreamManager::stream();
  DeviceStream* device_stream = dynamic_cast<DeviceStream*>(stream);

  if (is_live && device_stream) {
    QString ip = device_stream->zmqAddress();
    auto wait_dlg = new ZmqLoadDialog(ip, this);
    wait_dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(wait_dlg, &QDialog::rejected, this, &MainWindow::close);
    connect(&StreamManager::instance(), &StreamManager::eventsMerged, wait_dlg, &QDialog::accept);
    wait_dlg->show();
  } else {
    auto wait_dlg = new QProgressDialog(is_live ? tr("Waiting for live stream...") : tr("Loading segments..."),
                                        tr("&Abort"), 0, 100, this);

    wait_dlg->setWindowModality(Qt::WindowModal);
    wait_dlg->setAttribute(Qt::WA_DeleteOnClose);
    wait_dlg->setFixedSize(400, wait_dlg->sizeHint().height());

    connect(wait_dlg, &QProgressDialog::canceled, this, &MainWindow::close);
    connect(&StreamManager::instance(), &StreamManager::eventsMerged, wait_dlg, &QProgressDialog::accept);
    connect(&SystemRelay::instance(), &SystemRelay::downloadProgress, wait_dlg,
            [=](uint64_t cur, uint64_t total, bool success) {
              Q_UNUSED(success);
              if (total == 0) return;
              wait_dlg->setValue((int)((cur / (double)total) * 100));
            });
    wait_dlg->show();
  }
}

void MainWindow::eventsMerged() {
  auto* stream = StreamManager::stream();
  if (!stream) return;

  if (!stream->liveStreaming()) {
    const QString prev_fingerprint = car_fingerprint_;
    car_fingerprint_ = stream->carFingerprint();
    if (prev_fingerprint != car_fingerprint_) {
      video_dock_->setWindowTitle(tr("ROUTE: %1  FINGERPRINT: %2")
                                      .arg(stream->routeName())
                                      .arg(car_fingerprint_.isEmpty() ? tr("Unknown Car") : car_fingerprint_));
      // Don't overwrite already loaded DBC
      if (!GetDBC()->nonEmptyFileCount()) {
        QTimer::singleShot(0, this, [this]() { dbc_controller_->loadFromFingerprint(car_fingerprint_); });
      }
    }
  }
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
  if (obj == floating_window_ && event->type() == QEvent::Close) {
    toggleChartsDocking();
    return true;
  }
  return QMainWindow::eventFilter(obj, event);
}

void MainWindow::toggleChartsDocking() {
  if (floating_window_) {
    // Dock the charts widget back to the main window
    floating_window_->removeEventFilter(this);
    charts_layout_->insertWidget(0, charts_panel, 1);
    floating_window_->deleteLater();
    floating_window_ = nullptr;
    charts_panel->getToolBar()->setIsDocked(true);
  } else {
    // Float the charts widget in a separate window
    floating_window_ = new QWidget(this, Qt::Window);
    floating_window_->setWindowTitle("Charts");
    floating_window_->setLayout(new QVBoxLayout());
    floating_window_->layout()->addWidget(charts_panel);
    floating_window_->installEventFilter(this);
    floating_window_->showMaximized();
    charts_panel->getToolBar()->setIsDocked(false);
  }
}

void MainWindow::closeEvent(QCloseEvent* event) {
  // Force the StreamManager to clean up its resources
  StreamManager::instance().shutdown();

  dbc_controller_->remindSaveChanges();

  if (floating_window_) floating_window_->deleteLater();

  // save states
  settings.geometry = saveGeometry();
  settings.window_state = saveState();
  if (!StreamManager::instance().isLiveStream()) {
    settings.video_splitter_state = video_splitter_->saveState();
  }
  settings.message_header_state = message_list_->saveHeaderState();

  saveSessionState();
  SystemRelay::instance().uninstallHandlers();
  QMainWindow::closeEvent(event);
}

void MainWindow::setOption() {
  SettingsDialog dlg(this);
  dlg.exec();
}

void MainWindow::findSimilarBits() {
  FindSimilarBitsDlg* dlg = new FindSimilarBitsDlg(this);
  connect(dlg, &FindSimilarBitsDlg::openMessage, message_list_, &MessageList::selectMessage);
  dlg->show();
}

void MainWindow::findSignal() {
  FindSignalDlg* dlg = new FindSignalDlg(this);
  connect(dlg, &FindSignalDlg::openMessage, message_list_, &MessageList::selectMessage);
  dlg->show();
}

void MainWindow::onlineHelp() {
  if (auto guide = findChild<GuideOverlay*>()) {
    guide->close();
  } else {
    guide = new GuideOverlay(this);
    guide->setGeometry(rect());
    guide->show();
    guide->raise();
  }
}

void MainWindow::toggleFullScreen() {
  if (isFullScreen()) {
    menuBar()->show();
    statusBar()->show();
    showNormal();
    showMaximized();
  } else {
    menuBar()->hide();
    statusBar()->hide();
    showFullScreen();
  }
}

void MainWindow::saveSessionState() {
  settings.recent_dbc_file = "";
  settings.active_msg_id = "";
  settings.selected_msg_ids.clear();
  settings.active_charts.clear();

  for (const auto& f : GetDBC()->allFiles())
    if (!f->isEmpty()) {
      settings.recent_dbc_file = f->filename;
      break;
    }

  if (auto* detail = inspector_widget_->getMessageView()) {
    auto [active_id, ids] = detail->serializeMessageIds();
    settings.active_msg_id = active_id;
    settings.selected_msg_ids = ids;
  }
  if (charts_panel) settings.active_charts = charts_panel->serializeChartIds();
}

void MainWindow::restoreSessionState() {
  if (settings.recent_dbc_file.isEmpty() || GetDBC()->nonEmptyFileCount() == 0) return;

  QString dbc_file;
  for (const auto& f : GetDBC()->allFiles())
    if (!f->isEmpty()) {
      dbc_file = f->filename;
      break;
    }
  if (dbc_file != settings.recent_dbc_file) return;

  if (!settings.selected_msg_ids.isEmpty()) {
    inspector_widget_->getMessageView()->restoreTabs(settings.active_msg_id, settings.selected_msg_ids);
    inspector_widget_->setMessage(MessageId::fromString(settings.active_msg_id));
  }

  if (charts_panel != nullptr && !settings.active_charts.empty())
    charts_panel->restoreChartsFromIds(settings.active_charts);
}

void MainWindow::changeEvent(QEvent* ev) {
  if (ev->type() == QEvent::ApplicationPaletteChange) {
    utils::setTheme(0);
  }
  QMainWindow::changeEvent(ev);
}
