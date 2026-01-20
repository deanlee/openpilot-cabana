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
  MessageList(QWidget *parent);
  void selectMessage(const MessageId &message_id);
  QByteArray saveHeaderState() const { return view->header()->saveState(); }
  bool restoreHeaderState(const QByteArray &state) const { return view->header()->restoreState(state); }
  void suppressHighlighted();

signals:
  void msgSelectionChanged(const MessageId &message_id);
  void titleChanged(const QString &title);

protected:
  void setupConnections();
  QWidget *createToolBar();
  void resetState();
  void headerContextMenuEvent(const QPoint &pos);
  void menuAboutToShow();
  void setMultiLineBytes(bool multi);
  void updateTitle();
  void handleSelectionChanged(const QModelIndex &current);

  MessageTable *view;
  MessageHeader *header;
  MessageDelegate *delegate;
  std::optional<MessageId> current_msg_id;
  MessageModel *model;
  ToolButton *suppress_add;
  ToolButton *suppress_clear;
  QCheckBox *suppress_defined_signals;
  QMenu *menu;
  friend class MessageModel;
};
