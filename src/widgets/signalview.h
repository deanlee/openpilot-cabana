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
#include "signal_tree_view.h"

class SignalView : public QFrame {
  Q_OBJECT

public:
  SignalView(ChartsWidget *charts, QWidget *parent);
  void setMessage(const MessageId &id);
  void signalHovered(const cabana::Signal *sig);
  void updateChartState();
  void selectSignal(const cabana::Signal *sig, bool expand = false);
  SignalTreeModel *model = nullptr;

signals:
  void highlight(const cabana::Signal *sig);
  void showChart(const MessageId &id, const cabana::Signal *sig, bool show, bool merge);

private:
  void rowsChanged();
  void resizeEvent(QResizeEvent* event) override;
  void updateToolBar();
  void setSparklineRange(int value);
  void handleSignalAdded(MessageId id, const cabana::Signal *sig);
  void handleSignalUpdated(const cabana::Signal *sig);
  void updateState(const std::set<MessageId> *msgs = nullptr);
  std::pair<QModelIndex, QModelIndex> visibleSignalRange();

  int max_value_width = 0;
  int value_column_width = 0;
  SignalTreeView *tree;
  QLabel *sparkline_label;
  QSlider *sparkline_range_slider;
  QLineEdit *filter_edit;
  ChartsWidget *charts;
  QLabel *signal_count_lb;
  SignalTreeDelegate *delegate;

  friend class SignalTreeDelegate;
  friend class SignalTreeView;
};
