#pragma once

#include <QSet>
#include <QTableView>
#include <tuple>

#include "delegates/message_bytes.h"
#include "models/message_bytes.h"
#include "streams/abstractstream.h"

class BinaryView : public QTableView {
  Q_OBJECT

public:
  BinaryView(QWidget *parent = nullptr);
  void setMessage(const MessageId &message_id);
  void highlight(const cabana::Signal *sig);
  QSet<const cabana::Signal*> getOverlappingSignals() const;
  void updateState() { model->updateState(); }
  void paintEvent(QPaintEvent *event) override {
    is_message_active = can->isMessageActive(model->msg_id);
    QTableView::paintEvent(event);
  }
  QSize minimumSizeHint() const override;
  void setHeatmapLiveMode(bool live) { model->heatmap_live_mode = live; updateState(); }

signals:
  void signalClicked(const cabana::Signal *sig);
  void signalHovered(const cabana::Signal *sig);
  void editSignal(const cabana::Signal *origin_s, cabana::Signal &s);
  void showChart(const MessageId &id, const cabana::Signal *sig, bool show, bool merge);

private:
  void addShortcuts();
  void refresh();
  std::tuple<int, int, bool> getSelection(QModelIndex index);
  void setSelection(const QRect &rect, QItemSelectionModel::SelectionFlags flags) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void leaveEvent(QEvent *event) override;
  void highlightPosition(const QPoint &pt);

  QModelIndex anchor_index;
  MessageBytesModel *model;
  MessageBytesDelegate *delegate;
  bool is_message_active = false;
  const cabana::Signal *resize_sig = nullptr;
  const cabana::Signal *hovered_sig = nullptr;
  friend class MessageBytesDelegate;
};
