#pragma once

#include <optional>

#include "message_delegate.h"
#include "message_header.h"
#include "message_model.h"
#include "message_table.h"

class QCheckBox;
class QMenu;
class QPushButton;
class ToolButton;

class MessageList : public QWidget {
  Q_OBJECT

 public:
  MessageList(QWidget* parent);
  QByteArray saveHeaderState() const { return view->header()->saveState(); }
  bool restoreHeaderState(const QByteArray& state) const { return view->header()->restoreState(state); }
  void suppressHighlighted(bool suppress);
  void selectMessage(const MessageId& message_id) { selectMessageForced(message_id, false); }

 signals:
  void msgSelectionChanged(const MessageId& message_id);
  void titleChanged(const QString& title);

 protected:
  void setupConnections();
  void selectMessageForced(const MessageId& msg_id, bool force);
  QWidget* createToolBar();
  void resetState();
  void headerContextMenuEvent(const QPoint& pos);
  void menuAboutToShow();
  void updateTitle();
  void handleSelectionChanged(const QModelIndex& current);

  MessageTable* view;
  MessageHeader* header;
  MessageDelegate* delegate;
  std::optional<MessageId> current_msg_id;
  MessageModel* model;
  ToolButton* suppress_add;
  ToolButton* suppress_clear;
  QCheckBox* suppress_defined_signals;
  QMenu* menu;
};
