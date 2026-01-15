#pragma once

#include <QTreeView>

#include "core/dbc/dbc_message.h"

struct SignalTree : public QTreeView {
  Q_OBJECT
 public:
  SignalTree(QWidget* parent);
  void dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles = QVector<int>()) override;
  void leaveEvent(QEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;

 signals:
  void highlightRequested(const dbc::Signal* sig);

 private:
  void updateHighlight(const dbc::Signal* sig);

 private:
  const dbc::Signal* last_sig = nullptr;
};
