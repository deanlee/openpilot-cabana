#pragma once

#include <memory>
#include <set>
#include <utility>

#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QTreeView>

#include "chart/chartswidget.h"
#include "chart/sparkline.h"
#include "delegates/signal_tree.h"
#include "models/signal_tree.h"

class SignalView : public QFrame {
  Q_OBJECT

public:
  SignalView(ChartsWidget *charts, QWidget *parent);
  void setMessage(const MessageId &id);
  void signalHovered(const cabana::Signal *sig);
  void updateChartState();
  void selectSignal(const cabana::Signal *sig, bool expand = false);
  void rowClicked(const QModelIndex &index);
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

  struct TreeView : public QTreeView {
    TreeView(QWidget *parent) : QTreeView(parent) {}
    void rowsInserted(const QModelIndex &parent, int start, int end) override {
      ((SignalView *)parentWidget())->rowsChanged();
      // update widget geometries in QTreeView::rowsInserted
      QTreeView::rowsInserted(parent, start, end);
    }
    void dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles = QVector<int>()) override {
      // Bypass the slow call to QTreeView::dataChanged.
      QAbstractItemView::dataChanged(topLeft, bottomRight, roles);
    }
    void leaveEvent(QEvent *event) override {
      emit static_cast<SignalView *>(parentWidget())->highlight(nullptr);
      QTreeView::leaveEvent(event);
    }
  };
  int max_value_width = 0;
  int value_column_width = 0;
  TreeView *tree;
  QLabel *sparkline_label;
  QSlider *sparkline_range_slider;
  QLineEdit *filter_edit;
  ChartsWidget *charts;
  QLabel *signal_count_lb;
  SignalTreeDelegate *delegate;
};
