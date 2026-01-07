#pragma once

#include <QComboBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QTableView>

#include "common.h"
#include "delegates/message_table.h"
#include "models/message_history.h"
#include "streams/abstractstream.h"

class HistoryHeader : public QHeaderView {
public:
  HistoryHeader(Qt::Orientation orientation, QWidget *parent = nullptr) : QHeaderView(orientation, parent) {}
  QSize sectionSizeFromContents(int logicalIndex) const override;
  void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const;
};

class MessageHistory : public QFrame {
  Q_OBJECT

public:
  MessageHistory(QWidget *parent);
  void setMessage(const MessageId &message_id) { model->setMessage(message_id); }
  void updateState() { model->updateState(); }
  void showEvent(QShowEvent *event) override { model->updateState(true); }

private slots:
  void filterChanged();
  void exportToCSV();
  void modelReset();

private:
  QTableView *logs;
  MessageHistoryModel *model;
  QComboBox *signals_cb, *comp_box, *display_type_cb;
  QLineEdit *value_edit;
  QWidget *filters_widget;
  ToolButton *export_btn;
  MessageTableDelegate *delegate;
};
