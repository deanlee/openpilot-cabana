#include "mainwin.h"

#include <QApplication>
#include <QDesktopWidget>
#include <QFile>
#include <QFileDialog>
#include <QMenuBar>
#include <QProgressDialog>
#include <QShortcut>
#include <QUndoView>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>
#include <iostream>
#include <string>

#include "core/commands/commands.h"
#include "core/streams/abstractstream.h"
#include "modules/dbc/dbc_controller.h"
#include "modules/dbc/export.h"
#include "modules/settings/settings_dialog.h"
#include "modules/streams/stream_selector.h"
#include "modules/system/system_relay.h"
#include "modules/system/stream_manager.h"
#include "replay/include/http.h"
#include "tools/findsignal.h"
#include "widgets/guide_overlay.h"

MainWindow::MainWindow(AbstractStream *stream, const QString &dbc_file) : QMainWindow() {
  createDockWindows();
  setCentralWidget(center_widget = new CenterWidget(this));

  dbc_ = new DbcController(this);

  createActions();
  createStatusBar();
  createShortcuts();

  // save default window state to allow resetting it
  default_state = saveState();

  // restore states
  restoreGeometry(settings.geometry);
  if (isMaximized()) {
    setGeometry(QApplication::desktop()->availableGeometry(this));
  }
  restoreState(settings.window_state);

  // install handlers
  auto& relay = SystemRelay::instance();
  connect(&relay, &SystemRelay::logMessage, statusBar(), &QStatusBar::showMessage);
  connect(&relay, &SystemRelay::downloadProgress, this, &MainWindow::updateDownloadProgress);
  relay.installGlobalHandlers();

  setStyleSheet(QString(R"(QMainWindow::separator {
    width: %1px; /* when vertical */
    height: %1px; /* when horizontal */
  })").arg(style()->pixelMetric(QStyle::PM_SplitterWidth)));

  connect(GetDBC(), &dbc::Manager::DBCFileChanged, this, &MainWindow::DBCFileChanged);
  connect(UndoStack::instance(), &QUndoStack::cleanChanged, this, &MainWindow::undoStackCleanChanged);
  connect(&settings, &Settings::changed, this, &MainWindow::updateStatus);
  connect(&StreamManager::instance(), &StreamManager::eventsMerged, this, &MainWindow::eventsMerged);

  QTimer::singleShot(0, this, [=]() { stream ? openStream(stream, dbc_file) : selectAndOpenStream(); });
  show();
}

void MainWindow::createActions() {
  // File menu
  QMenu *file_menu = menuBar()->addMenu(tr("&File"));
  file_menu->addAction(tr("Open Stream..."), this, &MainWindow::selectAndOpenStream);
  close_stream_act = file_menu->addAction(tr("Close stream"), this, &MainWindow::closeStream);
  export_to_csv_act = file_menu->addAction(tr("Export to CSV..."), this, &MainWindow::exportToCSV);
  close_stream_act->setEnabled(false);
  export_to_csv_act->setEnabled(false);
  file_menu->addSeparator();

  file_menu->addAction(tr("New DBC File"), [this]() { dbc_->newFile(); }, QKeySequence::New);
  file_menu->addAction(tr("Open DBC File..."), [this]() { dbc_->openFile(); }, QKeySequence::Open);

  manage_dbcs_menu = file_menu->addMenu(tr("Manage &DBC Files"));
  connect(manage_dbcs_menu, &QMenu::aboutToShow, this, &MainWindow::updateLoadSaveMenus);

  open_recent_menu = file_menu->addMenu(tr("Open &Recent"));
  connect(open_recent_menu, &QMenu::aboutToShow, this, [this]() { dbc_->populateRecentMenu(open_recent_menu); });

  file_menu->addSeparator();
  QMenu *load_opendbc_menu = file_menu->addMenu(tr("Load DBC from commaai/opendbc"));
  dbc_->populateOpendbcFiles(load_opendbc_menu);

  file_menu->addAction(tr("Load DBC From Clipboard"), [=]() { dbc_->loadFromClipboard(); });

  file_menu->addSeparator();
  save_dbc = file_menu->addAction(tr("Save DBC..."), dbc_, &DbcController::save, QKeySequence::Save);
  save_dbc_as = file_menu->addAction(tr("Save DBC As..."), dbc_, &DbcController::saveAs, QKeySequence::SaveAs);
  copy_dbc_to_clipboard = file_menu->addAction(tr("Copy DBC To Clipboard"), dbc_, &DbcController::saveToClipboard);

  file_menu->addSeparator();
  file_menu->addAction(tr("Settings..."), this, &MainWindow::setOption, QKeySequence::Preferences);

  file_menu->addSeparator();
  file_menu->addAction(tr("E&xit"), qApp, &QApplication::closeAllWindows, QKeySequence::Quit);

  // Edit Menu
  QMenu *edit_menu = menuBar()->addMenu(tr("&Edit"));
  auto undo_act = UndoStack::instance()->createUndoAction(this, tr("&Undo"));
  undo_act->setShortcuts(QKeySequence::Undo);
  edit_menu->addAction(undo_act);
  auto redo_act = UndoStack::instance()->createRedoAction(this, tr("&Redo"));
  redo_act->setShortcuts(QKeySequence::Redo);
  edit_menu->addAction(redo_act);
  edit_menu->addSeparator();

  QMenu *commands_menu = edit_menu->addMenu(tr("Command &List"));
  QWidgetAction *commands_act = new QWidgetAction(this);
  commands_act->setDefaultWidget(new QUndoView(UndoStack::instance()));
  commands_menu->addAction(commands_act);

  // View Menu
  QMenu *view_menu = menuBar()->addMenu(tr("&View"));
  auto act = view_menu->addAction(tr("Full Screen"), this, &MainWindow::toggleFullScreen, QKeySequence::FullScreen);
  addAction(act);
  view_menu->addSeparator();
  view_menu->addAction(messages_dock->toggleViewAction());
  view_menu->addAction(video_dock->toggleViewAction());
  view_menu->addSeparator();
  view_menu->addAction(tr("Reset Window Layout"), [this]() { restoreState(default_state); });

  // Tools Menu
  tools_menu = menuBar()->addMenu(tr("&Tools"));
  tools_menu->addAction(tr("Find &Similar Bits"), this, &MainWindow::findSimilarBits);
  tools_menu->addAction(tr("&Find Signal"), this, &MainWindow::findSignal);

  // Help Menu
  QMenu *help_menu = menuBar()->addMenu(tr("&Help"));
  help_menu->addAction(tr("Help"), this, &MainWindow::onlineHelp, QKeySequence::HelpContents);
  help_menu->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

void MainWindow::createDockWindows() {
  messages_dock = new QDockWidget(tr("MESSAGES"), this);
  messages_dock->setObjectName("MessagesPanel");
  messages_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
  messages_dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
  addDockWidget(Qt::LeftDockWidgetArea, messages_dock);

  video_dock = new QDockWidget("", this);
  video_dock->setObjectName(tr("VideoPanel"));
  video_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  video_dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
  addDockWidget(Qt::RightDockWidgetArea, video_dock);
}

void MainWindow::createDockWidgets() {
  message_list = new MessageList(this);
  messages_dock->setWidget(message_list);
  connect(message_list, &MessageList::titleChanged, messages_dock, &QDockWidget::setWindowTitle);
  connect(message_list, &MessageList::msgSelectionChanged, center_widget, &CenterWidget::setMessage);

  // right panel
  charts_widget = new ChartsPanel(this);
  QWidget *charts_container = new QWidget(this);
  charts_layout = new QVBoxLayout(charts_container);
  charts_layout->setContentsMargins(0, 0, 0, 0);
  charts_layout->addWidget(charts_widget);

  // splitter between video and charts
  video_splitter = new QSplitter(Qt::Vertical, this);
  video_widget = new VideoPlayer(this);
  video_splitter->addWidget(video_widget);

  video_splitter->addWidget(charts_container);
  video_splitter->setStretchFactor(1, 1);
  video_splitter->restoreState(settings.video_splitter_state);
  video_splitter->handle(1)->setEnabled(!StreamManager::instance().isLiveStream());
  video_dock->setWidget(video_splitter);
  connect(charts_widget, &ChartsPanel::toggleChartsDocking, this, &MainWindow::toggleChartsDocking);
  connect(charts_widget, &ChartsPanel::showTip, video_widget, &VideoPlayer::showThumbnail);
}

void MainWindow::createStatusBar() {
  progress_bar = new QProgressBar();
  progress_bar->setRange(0, 100);
  progress_bar->setTextVisible(true);
  progress_bar->setFixedSize({300, 16});
  progress_bar->setVisible(false);
  statusBar()->addWidget(new QLabel(tr("For Help, Press F1")));
  statusBar()->addPermanentWidget(progress_bar);
  statusBar()->addPermanentWidget(status_label = new QLabel(this));
  updateStatus();
}

void MainWindow::createShortcuts() {
  auto shortcut = new QShortcut(QKeySequence(Qt::Key_Space), this, nullptr, nullptr, Qt::ApplicationShortcut);
  connect(shortcut, &QShortcut::activated, this, []() {
    StreamManager::stream()->pause(!StreamManager::stream()->isPaused());
  });
  // TODO: add more shortcuts here.
}

void MainWindow::undoStackCleanChanged(bool clean) {
  setWindowModified(!clean);
}

void MainWindow::DBCFileChanged() {
  UndoStack::instance()->clear();

  // Update file menu
  int cnt = GetDBC()->nonEmptyDBCCount();
  save_dbc->setText(cnt > 1 ? tr("Save %1 DBCs...").arg(cnt) : tr("Save DBC..."));
  save_dbc->setEnabled(cnt > 0);
  save_dbc_as->setEnabled(cnt == 1);
  // TODO: Support clipboard for multiple files
  copy_dbc_to_clipboard->setEnabled(cnt == 1);
  manage_dbcs_menu->setEnabled(StreamManager::instance().hasStream());

  QStringList title;
  for (auto f : GetDBC()->allDBCFiles()) {
    title.push_back(tr("(%1) %2").arg(toString(GetDBC()->sources(f)), f->name()));
  }
  setWindowFilePath(title.join(" | "));

  QTimer::singleShot(0, this, &::MainWindow::restoreSessionState);
}

void MainWindow::selectAndOpenStream() {
  StreamSelector dlg(this);
  if (dlg.exec()) {
    openStream(dlg.stream(), dlg.dbcFile());
  }
}

void MainWindow::closeStream() {
  openStream(new DummyStream(this));
  if (GetDBC()->nonEmptyDBCCount() > 0) {
    emit GetDBC()->DBCFileChanged();
  }
  statusBar()->showMessage(tr("stream closed"));
}

void MainWindow::exportToCSV() {
  QString dir = QString("%1/%2.csv").arg(settings.last_dir).arg(StreamManager::stream()->routeName());
  QString fn = QFileDialog::getSaveFileName(this, "Export stream to CSV file", dir, tr("csv (*.csv)"));
  if (!fn.isEmpty()) {
    exportMessagesToCSV(fn);
  }
}

void MainWindow::openStream(AbstractStream *stream, const QString &dbc_file) {
  StreamManager::instance().setStream(stream, dbc_file);

  center_widget->clear();
  delete message_list;
  delete video_splitter;

  dbc_->loadFile(dbc_file);

  bool has_stream = StreamManager::instance().hasStream();
  bool is_live_stream = StreamManager::instance().isLiveStream();
  close_stream_act->setEnabled(has_stream);
  export_to_csv_act->setEnabled(has_stream);
  tools_menu->setEnabled(has_stream);
  createDockWidgets();

  video_dock->setWindowTitle(StreamManager::stream()->routeName());
  if (is_live_stream || video_splitter->sizes()[0] == 0) {
    // display video at minimum size.
    video_splitter->setSizes({1, 1});
  }
  // Don't overwrite already loaded DBC
  if (!GetDBC()->nonEmptyDBCCount()) {
    dbc_->newFile();
  }

  if (has_stream) {
    auto wait_dlg = new QProgressDialog(
        is_live_stream ? tr("Waiting for the live stream to start...") : tr("Loading segment data..."),
        tr("&Abort"), 0, 100, this);
    wait_dlg->setWindowModality(Qt::WindowModal);
    wait_dlg->setFixedSize(400, wait_dlg->sizeHint().height());
    connect(wait_dlg, &QProgressDialog::canceled, this, &MainWindow::close);
    connect(&StreamManager::instance(), &StreamManager::eventsMerged, wait_dlg, &QProgressDialog::deleteLater);
    connect(&SystemRelay::instance(), &SystemRelay::downloadProgress, wait_dlg, [=](uint64_t cur, uint64_t total, bool success) {
      wait_dlg->setValue((int)((cur / (double)total) * 100));
    });
  }
}

void MainWindow::eventsMerged() {
  auto *stream = StreamManager::stream();
  if (!stream->liveStreaming() && std::exchange(car_fingerprint, stream->carFingerprint()) != car_fingerprint) {
    video_dock->setWindowTitle(tr("ROUTE: %1  FINGERPRINT: %2")
                                    .arg(stream->routeName())
                                    .arg(car_fingerprint.isEmpty() ? tr("Unknown Car") : car_fingerprint));
    // Don't overwrite already loaded DBC
    if (!GetDBC()->nonEmptyDBCCount()) {
      QTimer::singleShot(0, this, [this]() { dbc_->loadFromFingerprint(car_fingerprint); });
    }
  }
}

void MainWindow::updateLoadSaveMenus() {
  manage_dbcs_menu->clear();

  for (int source : StreamManager::stream()->sources) {
    if (source >= 64) continue; // Sent and blocked buses are handled implicitly

    SourceSet ss = {source, uint8_t(source + 128), uint8_t(source + 192)};

    QMenu *bus_menu = new QMenu(this);
    bus_menu->addAction(tr("New DBC File..."), [=]() { dbc_->newFile(ss); });
    bus_menu->addAction(tr("Open DBC File..."), [=]() { dbc_->openFile(ss); });
    bus_menu->addAction(tr("Load DBC From Clipboard..."), [=]() { dbc_->loadFromClipboard(ss, false); });

    // Show sub-menu for each dbc for this source.
    auto dbc_file = GetDBC()->findDBCFile(source);
    if (dbc_file) {
      bus_menu->addSeparator();
      bus_menu->addAction(dbc_file->name() + " (" + toString(GetDBC()->sources(dbc_file)) + ")")->setEnabled(false);
      bus_menu->addAction(tr("Save..."), [=]() { dbc_->saveFile(dbc_file); });
      bus_menu->addAction(tr("Save As..."), [=]() { dbc_->saveFileAs(dbc_file); });
      bus_menu->addAction(tr("Copy to Clipboard..."), [=]() { dbc_->saveFileToClipboard(dbc_file); });
      bus_menu->addAction(tr("Remove from this bus..."), [=]() { dbc_->closeFile(ss); });
      bus_menu->addAction(tr("Remove from all buses..."), [=]() { dbc_->closeFile(dbc_file); });
    }
    bus_menu->setTitle(tr("Bus %1 (%2)").arg(source).arg(dbc_file ? dbc_file->name() : "No DBCs loaded"));

    manage_dbcs_menu->addMenu(bus_menu);
  }
}

void MainWindow::updateDownloadProgress(uint64_t cur, uint64_t total, bool success) {
  if (success && cur < total) {
    progress_bar->setValue((cur / (double)total) * 100);
    progress_bar->setFormat(tr("Downloading %p% (%1)").arg(formattedDataSize(total).c_str()));
    progress_bar->show();
  } else {
    progress_bar->hide();
  }
}

void MainWindow::updateStatus() {
  status_label->setText(tr("Cached Minutes:%1 FPS:%2").arg(settings.max_cached_minutes).arg(settings.fps));
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
  if (obj == floating_window && event->type() == QEvent::Close) {
    toggleChartsDocking();
    return true;
  }
  return QMainWindow::eventFilter(obj, event);
}

void MainWindow::toggleChartsDocking() {
  if (floating_window) {
    // Dock the charts widget back to the main window
    floating_window->removeEventFilter(this);
    charts_layout->insertWidget(0, charts_widget, 1);
    floating_window->deleteLater();
    floating_window = nullptr;
    charts_widget->setIsDocked(true);
  } else {
    // Float the charts widget in a separate window
    floating_window = new QWidget(this, Qt::Window);
    floating_window->setWindowTitle("Charts");
    floating_window->setLayout(new QVBoxLayout());
    floating_window->layout()->addWidget(charts_widget);
    floating_window->installEventFilter(this);
    floating_window->showMaximized();
    charts_widget->setIsDocked(false);
  }
}

void MainWindow::closeEvent(QCloseEvent *event) {
  // Force the StreamManager to clean up its resources
  StreamManager::instance().shutdown();

  dbc_->remindSaveChanges();

  if (floating_window)
    floating_window->deleteLater();

  // save states
  settings.geometry = saveGeometry();
  settings.window_state = saveState();
  if (!StreamManager::instance().isLiveStream()) {
    settings.video_splitter_state = video_splitter->saveState();
  }
  if (message_list) {
    settings.message_header_state = message_list->saveHeaderState();
  }

  saveSessionState();
  SystemRelay::instance().uninstallHandlers();
  QWidget::closeEvent(event);
}

void MainWindow::setOption() {
  SettingsDialog dlg(this);
  dlg.exec();
}

void MainWindow::findSimilarBits() {
  FindSimilarBitsDlg *dlg = new FindSimilarBitsDlg(this);
  connect(dlg, &FindSimilarBitsDlg::openMessage, message_list, &MessageList::selectMessage);
  dlg->show();
}

void MainWindow::findSignal() {
  FindSignalDlg *dlg = new FindSignalDlg(this);
  connect(dlg, &FindSignalDlg::openMessage, message_list, &MessageList::selectMessage);
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

  for (auto &f : GetDBC()->allDBCFiles())
    if (!f->isEmpty()) { settings.recent_dbc_file = f->filename; break; }

  if (auto *detail = center_widget->getMessageDetails()) {
    auto [active_id, ids] = detail->serializeMessageIds();
    settings.active_msg_id = active_id;
    settings.selected_msg_ids = ids;
  }
  if (charts_widget)
    settings.active_charts = charts_widget->serializeChartIds();
}

void MainWindow::restoreSessionState() {
  if (settings.recent_dbc_file.isEmpty() || GetDBC()->nonEmptyDBCCount() == 0) return;

  QString dbc_file;
  for (auto& f : GetDBC()->allDBCFiles())
    if (!f->isEmpty()) { dbc_file = f->filename; break; }
  if (dbc_file != settings.recent_dbc_file) return;

  if (!settings.selected_msg_ids.isEmpty())
    center_widget->ensureMessageDetails()->restoreTabs(settings.active_msg_id, settings.selected_msg_ids);

  if (charts_widget != nullptr && !settings.active_charts.empty())
    charts_widget->restoreChartsFromIds(settings.active_charts);
}
