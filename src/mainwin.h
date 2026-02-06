#pragma once

#include <QDockWidget>
#include <QMainWindow>
#include <QMenu>
#include <QSplitter>

#include "core/dbc/dbc_manager.h"
#include "modules/charts/charts_panel.h"
#include "modules/inspector/message_inspector.h"
#include "modules/message_list/message_list.h"
#include "modules/video/video_player.h"
#include "tools/findsimilarbits.h"
#include "widgets/panel_splitter.h"
#include "widgets/status_bar.h"

class DbcController;
class QVBoxLayout;

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  MainWindow(AbstractStream* stream, const QString& dbc_file);
  void toggleChartsDocking();
  ChartsPanel* charts_panel = nullptr;

 public slots:
  void selectAndOpenStream();
  void openStream(AbstractStream* stream, const QString& dbc_file = {});
  void closeStream();
  void exportToCSV();
  void onStreamChanged();

 protected:
  void setupConnections();
  bool eventFilter(QObject* obj, QEvent* event) override;
  void changeEvent(QEvent* ev) override;
  void remindSaveChanges();

  void setupMenus();
  void createFileMenu();
  void createEditMenu();
  void createViewMenu();
  void createToolsMenu();
  void createHelpMenu();

  void setupDocks();
  void createMessagesDock();
  void createVideoChartsDock();

  void createLoadingDialog(bool is_live);
  void createShortcuts();
  void closeEvent(QCloseEvent* event) override;
  void DBCFileChanged();
  void setOption();
  void findSimilarBits();
  void findSignal();
  void undoStackCleanChanged(bool clean);
  void onlineHelp();
  void toggleFullScreen();
  void eventsMerged();
  void saveSessionState();
  void restoreSessionState();

  VideoPlayer* video_player_ = nullptr;
  QDockWidget* video_dock_ = nullptr;
  QDockWidget* messages_dock_ = nullptr;
  MessageList* message_list_ = nullptr;
  MessageInspector* inspector_widget_ = nullptr;
  QWidget* floating_window_ = nullptr;
  QVBoxLayout* charts_layout_ = nullptr;
  PanelSplitter* video_splitter_ = nullptr;
  StatusBar* status_bar_ = nullptr;
  QMenu* recent_files_menu_ = nullptr;
  QMenu* manage_dbcs_menu_ = nullptr;
  QMenu* tools_menu_ = nullptr;
  QAction* close_stream_act_ = nullptr;
  QAction* export_to_csv_act_ = nullptr;
  QAction* save_dbc_ = nullptr;
  QAction* save_dbc_as_ = nullptr;
  QAction* copy_dbc_to_clipboard_ = nullptr;
  QString car_fingerprint_;
  QByteArray default_window_state_;
  DbcController* dbc_controller_ = nullptr;
};
