#pragma once

#include <QDockWidget>
#include <QMainWindow>
#include <QMenu>
#include <QProgressBar>
#include <QSplitter>
#include <QStatusBar>
#include <set>

#include "core/dbc/dbc_manager.h"
#include "modules/charts/charts_panel.h"
#include "modules/inspector/message_inspector.h"
#include "modules/message_list/message_list.h"
#include "modules/video/video_player.h"
#include "tools/findsimilarbits.h"

class DbcController;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(AbstractStream *stream, const QString &dbc_file);
  void toggleChartsDocking();
  void showStatusMessage(const QString &msg, int timeout = 0) { statusBar()->showMessage(msg, timeout); }
  ChartsPanel *charts_widget = nullptr;

public slots:
  void selectAndOpenStream();
  void openStream(AbstractStream *stream, const QString &dbc_file = {});
  void closeStream();
  void exportToCSV();

protected:
  void startStream(AbstractStream *stream, QString dbc_file);
  bool eventFilter(QObject *obj, QEvent *event) override;
  void remindSaveChanges();
  void createActions();
  void createDockWindows();
  void createStatusBar();
  void createShortcuts();
  void closeEvent(QCloseEvent *event) override;
  void DBCFileChanged();
  void updateDownloadProgress(uint64_t cur, uint64_t total, bool success);
  void setOption();
  void findSimilarBits();
  void findSignal();
  void undoStackCleanChanged(bool clean);
  void onlineHelp();
  void toggleFullScreen();
  void updateStatus();
  void updateLoadSaveMenus();
  void createDockWidgets();
  void eventsMerged();
  void saveSessionState();
  void restoreSessionState();

  VideoPlayer *video_widget = nullptr;
  QDockWidget *video_dock;
  QDockWidget *messages_dock;
  MessageList *message_list = nullptr;
  CenterWidget *center_widget;
  QWidget *floating_window = nullptr;
  QVBoxLayout *charts_layout;
  QProgressBar *progress_bar;
  QLabel *status_label;
  QSplitter *video_splitter = nullptr;
  enum { MAX_RECENT_FILES = 15 };
  QMenu *open_recent_menu = nullptr;
  QMenu *manage_dbcs_menu = nullptr;
  QMenu *tools_menu = nullptr;
  QAction *close_stream_act = nullptr;
  QAction *export_to_csv_act = nullptr;
  QAction *save_dbc = nullptr;
  QAction *save_dbc_as = nullptr;
  QAction *copy_dbc_to_clipboard = nullptr;
  QString car_fingerprint;
  QByteArray default_state;
  DbcController *dbc_ = nullptr;
};
