#include "mainwin.h"

#include <QApplication>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QScreen>
#include <QShortcut>
#include <QToolButton>
#include <QUndoView>
#include <QWidgetAction>

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
  view_menu->addAction(charts_dock_->toggleViewAction());
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
  video_player_ = new VideoPlayer(this);
  video_dock_ = new QDockWidget("", this);
  video_dock_->setObjectName("VideoPanel");
  video_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  video_dock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                           QDockWidget::DockWidgetClosable);
  video_dock_->setWidget(video_player_);
  addDockWidget(Qt::RightDockWidgetArea, video_dock_);

  charts_dock_ = new QDockWidget(tr("CHARTS"), this);
  charts_dock_->setObjectName("ChartsPanel");
  charts_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  charts_dock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                            QDockWidget::DockWidgetClosable);
  charts_dock_->setWidget(charts_panel);
  splitDockWidget(video_dock_, charts_dock_, Qt::Vertical);

  connect(charts_dock_, &QDockWidget::topLevelChanged,
          [this](bool floating) { charts_panel->getToolBar()->setIsDocked(!floating); });
  connect(charts_panel, &ChartsPanel::toggleChartsDocking, this, &MainWindow::toggleChartsDocking);
  connect(charts_panel, &ChartsPanel::showCursor, video_player_, &VideoPlayer::showThumbnail);

  // Custom title bar for charts dock (adds maximize button)
  auto *title_bar = new QWidget(charts_dock_);
  auto *tb_layout = new QHBoxLayout(title_bar);
  tb_layout->setContentsMargins(4, 0, 2, 0);
  tb_layout->setSpacing(0);
  tb_layout->addWidget(new QLabel(tr("CHARTS"), title_bar));
  tb_layout->addStretch();

  auto *maximize_btn = new QToolButton(title_bar);
  maximize_btn->setAutoRaise(true);
  maximize_btn->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
  maximize_btn->setToolTip(tr("Maximize"));
  tb_layout->addWidget(maximize_btn);

  auto *float_btn = new QToolButton(title_bar);
  float_btn->setAutoRaise(true);
  float_btn->setText(tr("Undock"));
  tb_layout->addWidget(float_btn);

  auto *close_btn = new QToolButton(title_bar);
  close_btn->setAutoRaise(true);
  close_btn->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
  close_btn->setToolTip(tr("Close"));
  tb_layout->addWidget(close_btn);

  connect(maximize_btn, &QToolButton::clicked, [this] {
    if (!charts_dock_->isFloating()) {
      charts_dock_->setFloating(true);
    }
    charts_dock_->isMaximized() ? charts_dock_->showNormal() : charts_dock_->showMaximized();
  });
  connect(float_btn, &QToolButton::clicked, this, &MainWindow::toggleChartsDocking);
  connect(close_btn, &QToolButton::clicked, charts_dock_, &QDockWidget::close);
  connect(charts_dock_, &QDockWidget::topLevelChanged, [float_btn](bool floating) {
    float_btn->setText(floating ? MainWindow::tr("Dock") : MainWindow::tr("Undock"));
  });
  charts_dock_->setTitleBarWidget(title_bar);
}

void MainWindow::createShortcuts() {
  auto shortcut = new QShortcut(QKeySequence(Qt::Key_Space), this, nullptr, nullptr, Qt::ApplicationShortcut);
  connect(shortcut, &QShortcut::activated, this,
          []() { StreamManager::stream()->pause(!StreamManager::stream()->isPaused()); });
  // TODO: add more shortcuts here.
}

void MainWindow::onStreamChanged() {}

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
  QString dir = QString("%1/%2.csv").arg(settings.last_dir).arg(StreamManager::stream()->routeName());
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

  video_dock_->setWindowTitle(sm.stream()->routeName());
  if (is_live_stream || video_dock_->height() == 0) {
    resizeDocks({video_dock_, charts_dock_}, {1, 1}, Qt::Vertical);
  }
  // Don't overwrite already loaded DBC
  if (!GetDBC()->nonEmptyFileCount()) {
    dbc_controller_->newFile();
  }

  if (has_stream) {
    createLoadingDialog(is_live_stream);
  }
}

void MainWindow::createLoadingDialog(bool is_live) {
  auto wait_dlg = new QProgressDialog(is_live ? tr("Waiting for live stream...") : tr("Loading segments..."),
                                      tr("&Abort"), 0, 100, this);

  wait_dlg->setWindowModality(Qt::WindowModal);
  wait_dlg->setAttribute(Qt::WA_DeleteOnClose);
  wait_dlg->setFixedSize(400, wait_dlg->sizeHint().height());

  connect(wait_dlg, &QProgressDialog::canceled, this, &MainWindow::close);
  connect(&StreamManager::instance(), &StreamManager::eventsMerged, wait_dlg, &QProgressDialog::accept);
  connect(&SystemRelay::instance(), &SystemRelay::downloadProgress, wait_dlg,
          [=](uint64_t cur, uint64_t total, bool success) { wait_dlg->setValue((int)((cur / (double)total) * 100)); });
  wait_dlg->show();
}

void MainWindow::eventsMerged() {
  auto* stream = StreamManager::stream();
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
  return QMainWindow::eventFilter(obj, event);
}

void MainWindow::toggleChartsDocking() {
  charts_dock_->setFloating(!charts_dock_->isFloating());
}

void MainWindow::closeEvent(QCloseEvent* event) {
  // Force the StreamManager to clean up its resources
  StreamManager::instance().shutdown();

  dbc_controller_->remindSaveChanges();

  // save states
  settings.geometry = saveGeometry();
  settings.window_state = saveState();
  settings.message_header_state = message_list_->saveHeaderState();

  saveSessionState();
  SystemRelay::instance().uninstallHandlers();
  QWidget::closeEvent(event);
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
