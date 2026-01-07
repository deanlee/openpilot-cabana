#pragma once

#include <QSplitter>
#include <QTabWidget>
#include <set>

#include "binary_view.h"
#include "chart/chartswidget.h"
#include "common.h"
#include "message_history.h"
#include "signal_editor.h"

class MessageDetails : public QWidget {
  Q_OBJECT

public:
  MessageDetails(ChartsWidget *charts, QWidget *parent);
  void setMessage(const MessageId &message_id);
  void refresh();
  std::pair<QString, QStringList> serializeMessageIds() const;
  void restoreTabs(const QString active_msg_id, const QStringList &msg_ids);

private:
  void createToolBar();
  int findOrAddTab(const MessageId& id);
  void showTabBarContextMenu(const QPoint &pt);
  void editMsg();
  void removeMsg();
  void updateState(const std::set<MessageId> *msgs = nullptr);

  MessageId msg_id;
  QLabel *warning_icon, *warning_label;
  ElidedLabel *name_label;
  QWidget *warning_widget;
  TabBar *tabbar;
  QTabWidget *tab_widget;
  QAction *action_remove_msg;
  MessageHistory *message_history;
  BinaryView *binary_view;
  SignalEditor *signal_editor;
  ChartsWidget *charts;
  QSplitter *splitter;
};

class CenterWidget : public QWidget {
  Q_OBJECT
public:
  CenterWidget(QWidget *parent);
  void setMessage(const MessageId &message_id) { ensureMessageDetails()->setMessage(message_id); }
  MessageDetails* getMessageDetails() { return details; }
  MessageDetails* ensureMessageDetails();
  void clear();

private:
  QWidget *createWelcomeWidget();
  MessageDetails *details = nullptr;
  QWidget *welcome_widget = nullptr;
};
