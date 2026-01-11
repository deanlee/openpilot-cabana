#pragma once

#include <QComboBox>
#include <QLineEdit>
#include <QTableView>

#include "common.h"
#include "core/streams/abstract_stream.h"
#include "history_header.h"
#include "history_model.h"
#include "modules/message_list/message_delegate.h"

class MessageHistory : public QFrame {
  Q_OBJECT

public:
  MessageHistory(QWidget *parent);
  void setMessage(const MessageId &message_id) { model->setMessage(message_id); }
  void clearMessage();
  void updateState() { model->updateState(); }
  void showEvent(QShowEvent *event) override { model->updateState(true); }

private slots:
  void filterChanged();
  void exportToCSV();
  void modelReset();
  void handleDisplayTypeChange(int index);

private:
  void setupConnections();

  QTableView *logs;
  MessageHistoryModel *model;
  QComboBox *signals_cb, *comp_box, *display_type_cb;
  QLineEdit *value_edit;
  QWidget *filters_widget;
  ToolButton *export_btn;
  MessageDelegate *delegate;
};
