#pragma once

#include <QWidget>

#include "modules/charts/charts_panel.h"
#include "modules/inspector/binary/binary_view.h"
#include "modules/inspector/history/message_history.h"
#include "modules/inspector/signal_editor/signal_editor.h"
#include "widgets/panel_splitter.h"

class QSplitter;

class MessageView : public QWidget {
  Q_OBJECT

public:
  MessageView(ChartsPanel *charts, QWidget *parent);
  void setMessage(const MessageId &message_id);
  void refresh();
  std::pair<QString, QStringList> serializeMessageIds() const;
  void restoreTabs(const QString active_msg_id, const QStringList &msg_ids);
  void resetState();
  void toggleCenterOrientation();

signals:
  void requestDockCompression();

private:
  void setupConnections();
  QWidget *createToolBar();
  int findOrAddTab(const MessageId& id);
  void showTabBarContextMenu(const QPoint &pt);
  void editMsg();
  void removeMsg();
  void updateState(const std::set<MessageId> *msgs = nullptr);
  void updateOrientationButton();

  MessageId msg_id;
  QLabel *warning_icon, *warning_label;
  ElidedLabel *name_label;
  QWidget *warning_widget;
  TabBar *tabbar;
  QTabWidget *tab_widget;
  ToolButton *remove_msg_btn;
  MessageHistory *message_history;
  BinaryView *binary_view;
  SignalEditor *signal_editor;
  ChartsPanel *charts;
  PanelSplitter *splitter;
  ToolButton *toggle_orientation_btn_;
};
