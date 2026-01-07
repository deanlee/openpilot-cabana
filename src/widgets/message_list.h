#pragma once

#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTimer>
#include <QTreeView>
#include <QWheelEvent>
#include <optional>

#include "delegates/message_table.h"
#include "models/message_table.h"

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

class MessageHeader : public QHeaderView {
  // https://stackoverflow.com/a/44346317
  Q_OBJECT
public:
  MessageHeader(QWidget *parent);
  void updateHeaderPositions();
  void updateGeometries() override;
  QSize sizeHint() const override;
  void updateFilters();

  QMap<int, QLineEdit *> editors;
  QTimer filter_timer;
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

  MessageTable *view;
  MessageHeader *header;
  MessageTableDelegate *delegate;
  std::optional<MessageId> current_msg_id;
  MessageTableModel *model;
  QPushButton *suppress_add;
  QPushButton *suppress_clear;
  QMenu *menu;
  friend class MessageTableModel;
};
