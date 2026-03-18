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
  QByteArray saveHeaderState() const { return table_->header()->saveState(); }
  bool restoreHeaderState(const QByteArray& state) const { return table_->header()->restoreState(state); }
  void suppressHighlighted(bool suppress);
  void selectMessage(const MessageId& message_id) { selectMessageForced(message_id, false); }

 signals:
  void msgSelectionChanged(const MessageId& message_id);
  void titleChanged(const QString& title);

 protected:
  // Construction helpers
  QWidget* createToolBar();
  void setupConnections();
  // State management
  void resetState();
  void updateTitle();
  void selectMessageForced(const MessageId& msg_id, bool force);
  // Event handlers
  void onCurrentChanged(const QModelIndex& current);
  void onHeaderContextMenuRequested(const QPoint& pos);
  void onMenuAboutToShow();

  // Core model/view
  MessageModel* model_;
  MessageTable* table_;
  MessageHeader* header_;
  MessageDelegate* delegate_;
  std::optional<MessageId> current_msg_id_;
  // Toolbar widgets
  ToolButton* mute_active_btn_;
  ToolButton* unmute_all_btn_;
  QCheckBox* suppress_defined_signals_;
  QMenu* menu_;
};
