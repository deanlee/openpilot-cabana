#pragma once

#include <QComboBox>
#include <QScrollBar>
#include <QTableView>
#include <QWheelEvent>

#include "core/streams/abstract_stream.h"
#include "history_header.h"
#include "history_model.h"
#include "modules/message_list/message_delegate.h"
#include "widgets/debounced_line_edit.h"
#include "widgets/tool_button.h"

class MessageHistory : public QFrame {
  Q_OBJECT

public:
  MessageHistory(QWidget *parent);
  void setMessage(const MessageId &message_id) { model->setMessage(message_id); }
  void clearMessage();
  void updateState() { model->updateState(); }

private slots:
  void filterChanged();
  void exportToCSV();
  void resetInternalState();
  void setHexModel(int index);

private:
  QWidget* createToolbar();
  void setupConnections();

  class HistoryTableView : public QTableView {
   public:
    using QTableView::QTableView;  // Inherit constructors

   protected:
    void wheelEvent(QWheelEvent* e) override {
      if (e->modifiers() & Qt::ShiftModifier) {
        int delta = e->angleDelta().y();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta);
        return;
      }
      QTableView::wheelEvent(e);
    }
  };

  HistoryTableView *logs;
  MessageHistoryModel *model;
  QComboBox *signals_cb, *comp_box, *display_type_cb;
  DebouncedLineEdit *value_edit;
  QWidget *filters_widget;
  ToolButton *export_btn;
  MessageDelegate *delegate;
};
