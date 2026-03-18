#pragma once

#include <QTableView>
#include <tuple>

#include "binary_delegate.h"
#include "binary_model.h"
#include "core/streams/abstract_stream.h"
#include "modules/system/stream_manager.h"

class BinaryView : public QTableView {
  Q_OBJECT

 public:
  BinaryView(QWidget* parent = nullptr);
  void setModel(QAbstractItemModel* newModel) override;
  void highlightSignal(const dbc::Signal* sig);
  QSize minimumSizeHint() const override;

 signals:
  void signalClicked(const dbc::Signal* sig);
  void signalHovered(const dbc::Signal* sig);
  void editSignal(const dbc::Signal* original_signal, dbc::Signal& s);
  void showChart(const MessageId& id, const dbc::Signal* sig, bool show, bool merge);

 private:
  void resetInternalState();
  void setupShortcuts();
  std::tuple<int, int, bool> calculateSelection(QModelIndex index);
  void setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags flags) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void highlightSignalAtPosition(const QPoint& pt);

  QModelIndex anchor_index_;
  BinaryModel* model;
  MessageBytesDelegate* delegate;
  const dbc::Signal* resizing_signal = nullptr;
  const dbc::Signal* hovered_signal = nullptr;
  friend class MessageBytesDelegate;
};
