#include "mainwin.h"

#include <algorithm>
#include <iostream>
#include <string>

#include <QApplication>
#include <QClipboard>
#include <QDesktopWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonObject>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QResizeEvent>
#include <QShortcut>
#include <QTextDocument>
#include <QUndoView>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "commands.h"
#include "tools/findsignal.h"
#include "utils/export.h"
#include "replay/include/http.h"
#include "streams/abstractstream.h"
#include "streams/widgets/stream_selector.h"
#include "widgets/settings_dialog.h"


MainWindow::MainWindow(AbstractStream *stream, const QString &dbc_file) : QMainWindow() {
  loadFingerprints();
  createDockWindows();
  setCentralWidget(center_widget = new CenterWidget(this));
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
  static auto static_main_win = this;
  qRegisterMetaType<uint64_t>("uint64_t");
  qRegisterMetaType<SourceSet>("SourceSet");
  installDownloadProgressHandler([](uint64_t cur, uint64_t total, bool success) {
    emit static_main_win->updateProgressBar(cur, total, success);
  });
  qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    if (type == QtDebugMsg) return;
    emit static_main_win->showMessage(msg, 2000);
  });
  installMessageHandler([](ReplyMsgType type, const std::string msg) { qInfo() << msg.c_str(); });

  setStyleSheet(QString(R"(QMainWindow::separator {
    width: %1px; /* when vertical */
    height: %1px; /* when horizontal */
  })").arg(style()->pixelMetric(QStyle::PM_SplitterWidth)));

  connect(this, &MainWindow::showMessage, statusBar(), &QStatusBar::showMessage);
  connect(this, &MainWindow::updateProgressBar, this, &MainWindow::updateDownloadProgress);
  connect(GetDBC(), &dbc::Manager::DBCFileChanged, this, &MainWindow::DBCFileChanged);
  connect(UndoStack::instance(), &QUndoStack::cleanChanged, this, &MainWindow::undoStackCleanChanged);
  connect(&settings, &Settings::changed, this, &MainWindow::updateStatus);

  QTimer::singleShot(0, this, [=]() { stream ? openStream(stream, dbc_file) : selectAndOpenStream(); });
  show();
}

void MainWindow::loadFingerprints() {
  QFile json_file(QApplication::applicationDirPath() + "/data/opendbc/car_fingerprint_to_dbc.json");
  if (json_file.open(QIODevice::ReadOnly)) {
    fingerprint_to_dbc = QJsonDocument::fromJson(json_file.readAll());
  }
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

  file_menu->addAction(tr("New DBC File"), [this]() { newFile(); }, QKeySequence::New);
  file_menu->addAction(tr("Open DBC File..."), [this]() { openFile(); }, QKeySequence::Open);

  manage_dbcs_menu = file_menu->addMenu(tr("Manage &DBC Files"));
  connect(manage_dbcs_menu, &QMenu::aboutToShow, this, &MainWindow::updateLoadSaveMenus);

  open_recent_menu = file_menu->addMenu(tr("Open &Recent"));
  connect(open_recent_menu, &QMenu::aboutToShow, this, &MainWindow::updateRecentFileMenu);

  file_menu->addSeparator();
  QMenu *load_opendbc_menu = file_menu->addMenu(tr("Load DBC from commaai/opendbc"));
  QString local_opendbc_path = QDir::current().absoluteFilePath("data/opendbc");
  QDir opendbc_dir(local_opendbc_path);
  if (opendbc_dir.exists()) {
    for (const auto& dbc_name : opendbc_dir.entryList({"*.dbc"}, QDir::Files, QDir::Name)) {
      load_opendbc_menu->addAction(dbc_name, [this, dbc_name]() {
        loadDBCFromOpendbc(dbc_name);
      });
    }
  } else {
    rWarning("opendbc folder not found at: %s", qPrintable(local_opendbc_path));
  }

  file_menu->addAction(tr("Load DBC From Clipboard"), [=]() { loadFromClipboard(); });

  file_menu->addSeparator();
  save_dbc = file_menu->addAction(tr("Save DBC..."), this, &MainWindow::save, QKeySequence::Save);
  save_dbc_as = file_menu->addAction(tr("Save DBC As..."), this, &MainWindow::saveAs, QKeySequence::SaveAs);
  copy_dbc_to_clipboard = file_menu->addAction(tr("Copy DBC To Clipboard"), this, &MainWindow::saveToClipboard);

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
  charts_widget = new ChartsWidget(this);
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
  video_splitter->handle(1)->setEnabled(!can->liveStreaming());
  video_dock->setWidget(video_splitter);
  connect(charts_widget, &ChartsWidget::toggleChartsDocking, this, &MainWindow::toggleChartsDocking);
  connect(charts_widget, &ChartsWidget::showTip, video_widget, &VideoPlayer::showThumbnail);
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
    if (can) can->pause(!can->isPaused());
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
  manage_dbcs_menu->setEnabled(dynamic_cast<DummyStream *>(can) == nullptr);

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
  } else if (!can) {
    openStream(new DummyStream(this));
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
  QString dir = QString("%1/%2.csv").arg(settings.last_dir).arg(can->routeName());
  QString fn = QFileDialog::getSaveFileName(this, "Export stream to CSV file", dir, tr("csv (*.csv)"));
  if (!fn.isEmpty()) {
    utils::exportToCSV(fn);
  }
}

void MainWindow::newFile(SourceSet s) {
  closeFile(s);
  GetDBC()->open(s, "", "");
}

void MainWindow::openFile(SourceSet s) {
  remindSaveChanges();
  QString fn = QFileDialog::getOpenFileName(this, tr("Open File"), settings.last_dir, "DBC (*.dbc)");
  if (!fn.isEmpty()) {
    loadFile(fn, s);
  }
}

void MainWindow::loadFile(const QString &fn, SourceSet s) {
  if (!fn.isEmpty()) {
    closeFile(s);

    QString error;
    if (GetDBC()->open(s, fn, &error)) {
      updateRecentFiles(fn);
      statusBar()->showMessage(tr("DBC File %1 loaded").arg(fn), 2000);
    } else {
      QMessageBox msg_box(QMessageBox::Warning, tr("Failed to load DBC file"), tr("Failed to parse DBC file %1").arg(fn));
      msg_box.setDetailedText(error);
      msg_box.exec();
    }
  }
}

void MainWindow::loadDBCFromOpendbc(const QString &name) {
  QString local_opendbc_path = QDir::current().absoluteFilePath("data/opendbc");
  loadFile(QString("%1/%2").arg(local_opendbc_path, name));
}

void MainWindow::loadFromClipboard(SourceSet s, bool close_all) {
  closeFile(s);

  QString dbc_str = QGuiApplication::clipboard()->text();
  QString error;
  bool ret = GetDBC()->open(s, "", dbc_str, &error);
  if (ret && GetDBC()->nonEmptyDBCCount() > 0) {
    QMessageBox::information(this, tr("Load From Clipboard"), tr("DBC Successfully Loaded!"));
  } else {
    QMessageBox msg_box(QMessageBox::Warning, tr("Failed to load DBC from clipboard"), tr("Make sure that you paste the text with correct format."));
    msg_box.setDetailedText(error);
    msg_box.exec();
  }
}

void MainWindow::openStream(AbstractStream *stream, const QString &dbc_file) {
  if (can) {
    connect(can, &QObject::destroyed, this, [=]() { startStream(stream, dbc_file); });
    can->deleteLater();
  } else {
    startStream(stream, dbc_file);
  }
}

void MainWindow::startStream(AbstractStream *stream, QString dbc_file) {
  center_widget->clear();
  delete message_list;
  delete video_splitter;

  can = stream;
  can->setParent(this);  // take ownership
  can->start();

  loadFile(dbc_file);
  statusBar()->showMessage(tr("Stream [%1] started").arg(can->routeName()), 2000);

  bool has_stream = dynamic_cast<DummyStream *>(can) == nullptr;
  close_stream_act->setEnabled(has_stream);
  export_to_csv_act->setEnabled(has_stream);
  tools_menu->setEnabled(has_stream);
  createDockWidgets();

  video_dock->setWindowTitle(can->routeName());
  if (can->liveStreaming() || video_splitter->sizes()[0] == 0) {
    // display video at minimum size.
    video_splitter->setSizes({1, 1});
  }
  // Don't overwrite already loaded DBC
  if (!GetDBC()->nonEmptyDBCCount()) {
    newFile();
  }

  connect(can, &AbstractStream::eventsMerged, this, &MainWindow::eventsMerged);

  if (has_stream) {
    auto wait_dlg = new QProgressDialog(
        can->liveStreaming() ? tr("Waiting for the live stream to start...") : tr("Loading segment data..."),
        tr("&Abort"), 0, 100, this);
    wait_dlg->setWindowModality(Qt::WindowModal);
    wait_dlg->setFixedSize(400, wait_dlg->sizeHint().height());
    connect(wait_dlg, &QProgressDialog::canceled, this, &MainWindow::close);
    connect(can, &AbstractStream::eventsMerged, wait_dlg, &QProgressDialog::deleteLater);
    connect(this, &MainWindow::updateProgressBar, wait_dlg, [=](uint64_t cur, uint64_t total, bool success) {
      wait_dlg->setValue((int)((cur / (double)total) * 100));
    });
  }
}

void MainWindow::eventsMerged() {
  if (!can->liveStreaming() && std::exchange(car_fingerprint, can->carFingerprint()) != car_fingerprint) {
    video_dock->setWindowTitle(tr("ROUTE: %1  FINGERPRINT: %2")
                                    .arg(can->routeName())
                                    .arg(car_fingerprint.isEmpty() ? tr("Unknown Car") : car_fingerprint));
    // Don't overwrite already loaded DBC
    if (!GetDBC()->nonEmptyDBCCount() && fingerprint_to_dbc.object().contains(car_fingerprint)) {
      QTimer::singleShot(0, this, [this]() { loadDBCFromOpendbc(fingerprint_to_dbc[car_fingerprint].toString() + ".dbc"); });
    }
  }
}

void MainWindow::save() {
  // Save all open DBC files
  for (auto dbc_file : GetDBC()->allDBCFiles()) {
    if (dbc_file->isEmpty()) continue;
    saveFile(dbc_file);
  }
}

void MainWindow::saveAs() {
  // Save as all open DBC files. Should not be called with more than 1 file open
  for (auto dbc_file : GetDBC()->allDBCFiles()) {
    if (dbc_file->isEmpty()) continue;
    saveFileAs(dbc_file);
  }
}

void MainWindow::closeFile(SourceSet s) {
  remindSaveChanges();
  if (s == SOURCE_ALL) {
    GetDBC()->closeAll();
  } else {
    GetDBC()->close(s);
  }
}

void MainWindow::closeFile(dbc::File *dbc_file) {
  assert(dbc_file != nullptr);
  remindSaveChanges();
  GetDBC()->close(dbc_file);
  // Ensure we always have at least one file open
  if (GetDBC()->dbcCount() == 0) {
    newFile();
  }
}

void MainWindow::saveFile(dbc::File *dbc_file) {
  assert(dbc_file != nullptr);
  if (!dbc_file->filename.isEmpty()) {
    dbc_file->save();
    UndoStack::instance()->setClean();
    statusBar()->showMessage(tr("File saved"), 2000);
  } else if (!dbc_file->isEmpty()) {
    saveFileAs(dbc_file);
  }
}

void MainWindow::saveFileAs(dbc::File *dbc_file) {
  QString title = tr("Save File (bus: %1)").arg(toString(GetDBC()->sources(dbc_file)));
  QString fn = QFileDialog::getSaveFileName(this, title, QDir::cleanPath(settings.last_dir + "/untitled.dbc"), tr("DBC (*.dbc)"));
  if (!fn.isEmpty()) {
    dbc_file->saveAs(fn);
    UndoStack::instance()->setClean();
    statusBar()->showMessage(tr("File saved as %1").arg(fn), 2000);
    updateRecentFiles(fn);
  }
}

void MainWindow::saveToClipboard() {
  // Copy all open DBC files to clipboard. Should not be called with more than 1 file open
  for (auto dbc_file : GetDBC()->allDBCFiles()) {
    if (dbc_file->isEmpty()) continue;
    saveFileToClipboard(dbc_file);
  }
}

void MainWindow::saveFileToClipboard(dbc::File *dbc_file) {
  assert(dbc_file != nullptr);
  QGuiApplication::clipboard()->setText(dbc_file->generateGetDBC());
  QMessageBox::information(this, tr("Copy To Clipboard"), tr("DBC Successfully copied!"));
}

void MainWindow::updateLoadSaveMenus() {
  manage_dbcs_menu->clear();

  for (int source : can->sources) {
    if (source >= 64) continue; // Sent and blocked buses are handled implicitly

    SourceSet ss = {source, uint8_t(source + 128), uint8_t(source + 192)};

    QMenu *bus_menu = new QMenu(this);
    bus_menu->addAction(tr("New DBC File..."), [=]() { newFile(ss); });
    bus_menu->addAction(tr("Open DBC File..."), [=]() { openFile(ss); });
    bus_menu->addAction(tr("Load DBC From Clipboard..."), [=]() { loadFromClipboard(ss, false); });

    // Show sub-menu for each dbc for this source.
    auto dbc_file = GetDBC()->findDBCFile(source);
    if (dbc_file) {
      bus_menu->addSeparator();
      bus_menu->addAction(dbc_file->name() + " (" + toString(GetDBC()->sources(dbc_file)) + ")")->setEnabled(false);
      bus_menu->addAction(tr("Save..."), [=]() { saveFile(dbc_file); });
      bus_menu->addAction(tr("Save As..."), [=]() { saveFileAs(dbc_file); });
      bus_menu->addAction(tr("Copy to Clipboard..."), [=]() { saveFileToClipboard(dbc_file); });
      bus_menu->addAction(tr("Remove from this bus..."), [=]() { closeFile(ss); });
      bus_menu->addAction(tr("Remove from all buses..."), [=]() { closeFile(dbc_file); });
    }
    bus_menu->setTitle(tr("Bus %1 (%2)").arg(source).arg(dbc_file ? dbc_file->name() : "No DBCs loaded"));

    manage_dbcs_menu->addMenu(bus_menu);
  }
}

void MainWindow::updateRecentFiles(const QString &fn) {
  settings.recent_files.removeAll(fn);
  settings.recent_files.prepend(fn);
  while (settings.recent_files.size() > MAX_RECENT_FILES) {
    settings.recent_files.removeLast();
  }
  settings.last_dir = QFileInfo(fn).absolutePath();
}

void MainWindow::updateRecentFileMenu() {
  open_recent_menu->clear();

  int num_recent_files = std::min<int>(settings.recent_files.size(), MAX_RECENT_FILES);
  if (!num_recent_files) {
    open_recent_menu->addAction(tr("No Recent Files"))->setEnabled(false);
    return;
  }

  for (int i = 0; i < num_recent_files; ++i) {
    QString text = tr("&%1 %2").arg(i + 1).arg(QFileInfo(settings.recent_files[i]).fileName());
    open_recent_menu->addAction(text, this, [this, file = settings.recent_files[i]]() { loadFile(file); });
  }
}

void MainWindow::remindSaveChanges() {
  while (!UndoStack::instance()->isClean()) {
    QString text = tr("You have unsaved changes. Press ok to save them, cancel to discard.");
    int ret = QMessageBox::question(this, tr("Unsaved Changes"), text, QMessageBox::Ok | QMessageBox::Cancel);
    if (ret != QMessageBox::Ok) break;
    save();
  }
  UndoStack::instance()->clear();
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
  remindSaveChanges();

  installDownloadProgressHandler(nullptr);
  qInstallMessageHandler(nullptr);

  if (floating_window)
    floating_window->deleteLater();

  // save states
  settings.geometry = saveGeometry();
  settings.window_state = saveState();
  if (can && !can->liveStreaming()) {
    settings.video_splitter_state = video_splitter->saveState();
  }
  if (message_list) {
    settings.message_header_state = message_list->saveHeaderState();
  }

  saveSessionState();
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
  if (auto help = findChild<HelpOverlay*>()) {
    help->close();
  } else {
    help = new HelpOverlay(this);
    help->setGeometry(rect());
    help->show();
    help->raise();
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

// HelpOverlay
HelpOverlay::HelpOverlay(MainWindow *parent) : QWidget(parent) {
  setAttribute(Qt::WA_NoSystemBackground, true);
  setAttribute(Qt::WA_TranslucentBackground, true);
  setAttribute(Qt::WA_DeleteOnClose);
  parent->installEventFilter(this);
}

void HelpOverlay::paintEvent(QPaintEvent *event) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor(0, 0, 0, 50));
  auto parent = parentWidget();
  drawHelpForWidget(painter, parent->findChild<MessageList *>());
  drawHelpForWidget(painter, parent->findChild<BinaryView *>());
  drawHelpForWidget(painter, parent->findChild<SignalEditor *>());
  drawHelpForWidget(painter, parent->findChild<ChartsWidget *>());
  drawHelpForWidget(painter, parent->findChild<VideoPlayer *>());
}

void HelpOverlay::drawHelpForWidget(QPainter &painter, QWidget *w) {
  if (w && w->isVisible() && !w->whatsThis().isEmpty()) {
    QPoint pt = mapFromGlobal(w->mapToGlobal(w->rect().center()));
    if (rect().contains(pt)) {
      QTextDocument document;
      document.setHtml(w->whatsThis());
      QSize doc_size = document.size().toSize();
      QPoint topleft = {pt.x() - doc_size.width() / 2, pt.y() - doc_size.height() / 2};
      painter.translate(topleft);
      painter.fillRect(QRect{{0, 0}, doc_size}, palette().toolTipBase());
      document.drawContents(&painter);
      painter.translate(-topleft);
    }
  }
}

bool HelpOverlay::eventFilter(QObject *obj, QEvent *event) {
  if (obj == parentWidget() && event->type() == QEvent::Resize) {
    QResizeEvent *resize_event = (QResizeEvent *)(event);
    setGeometry(QRect{QPoint(0, 0), resize_event->size()});
  }
  return false;
}

void HelpOverlay::mouseReleaseEvent(QMouseEvent *event) {
  close();
}
