#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSlider>
#include <memory>
#include <set>
#include <utility>

#include "modules/charts/charts_panel.h"
#include "modules/charts/sparkline.h"
#include "signal_tree.h"
#include "signal_tree_delegate.h"
#include "signal_tree_model.h"

class SignalEditor : public QFrame {
  Q_OBJECT

public:
  SignalEditor(ChartsPanel *charts, QWidget *parent);
  void setMessage(const MessageId &id);
  void clearMessage();
  void signalHovered(const dbc::Signal *sig);
  void updateChartState();
  void selectSignal(const dbc::Signal *sig, bool expand = false);
  SignalTreeModel *model = nullptr;

signals:
  void highlight(const dbc::Signal *sig);
  void showChart(const MessageId &id, const dbc::Signal *sig, bool show, bool merge);

private:
  QWidget *createToolbar();
  void setupConnections();
  void rowsChanged();
  void resizeEvent(QResizeEvent* event) override;
  void updateToolBar();
  void setSparklineRange(int value);
  void handleSignalAdded(MessageId id, const dbc::Signal *sig);
  void handleSignalUpdated(const dbc::Signal *sig);
  void updateState(const std::set<MessageId> *msgs = nullptr);
  void updateColumnWidths();
  std::pair<QModelIndex, QModelIndex> visibleSignalRange();

  int value_column_width = 0;
  SignalTree *tree;
  QLabel *sparkline_label;
  QSlider *sparkline_range_slider;
  QLineEdit *filter_edit;
  ChartsPanel *charts;
  QLabel *signal_count_lb;
  SignalTreeDelegate *delegate;
  ToolButton *collapse_btn;

  friend class SignalTreeDelegate;
  friend class SignalTree;
};
