#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSlider>
#include <memory>
#include <set>
#include <utility>

#include "chart/chartswidget.h"
#include "chart/sparkline.h"
#include "delegates/signal_tree.h"
#include "models/signal_tree.h"
#include "signal_tree.h"

class SignalEditor : public QFrame {
  Q_OBJECT

public:
  SignalEditor(ChartsWidget *charts, QWidget *parent);
  void setMessage(const MessageId &id);
  void signalHovered(const dbc::Signal *sig);
  void updateChartState();
  void selectSignal(const dbc::Signal *sig, bool expand = false);
  SignalTreeModel *model = nullptr;

signals:
  void highlight(const dbc::Signal *sig);
  void showChart(const MessageId &id, const dbc::Signal *sig, bool show, bool merge);

private:
  void rowsChanged();
  void resizeEvent(QResizeEvent* event) override;
  void updateToolBar();
  void setSparklineRange(int value);
  void handleSignalAdded(MessageId id, const dbc::Signal *sig);
  void handleSignalUpdated(const dbc::Signal *sig);
  void updateState(const std::set<MessageId> *msgs = nullptr);
  void updateColumnWidths();
  std::pair<QModelIndex, QModelIndex> visibleSignalRange();

  int max_value_width = 0;
  int value_column_width = 0;
  SignalTree *tree;
  QLabel *sparkline_label;
  QSlider *sparkline_range_slider;
  QLineEdit *filter_edit;
  ChartsWidget *charts;
  QLabel *signal_count_lb;
  SignalTreeDelegate *delegate;

  friend class SignalTreeDelegate;
  friend class SignalTree;
};
