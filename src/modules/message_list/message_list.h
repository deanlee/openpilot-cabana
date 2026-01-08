#pragma once

#include <QMenu>
#include <QPushButton>
#include <QTimer>
#include <QTreeView>
#include <QWheelEvent>
#include <optional>

#include "message_delegate.h"
#include "message_header.h"
#include "message_model.h"

class MessageTable : public QTreeView {
  Q_OBJECT
public:
  MessageTable(QWidget *parent) : QTreeView(parent) {}
  void updateBytesSectionSize();

protected:
  void drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const override {}
  void dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles = QVector<int>()) override;
  void wheelEvent(QWheelEvent *event) override;
};

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
  QWidget *createToolBar();
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
  QPushButton *suppress_add;
  QPushButton *suppress_clear;
  QMenu *menu;
  friend class MessageModel;
};
