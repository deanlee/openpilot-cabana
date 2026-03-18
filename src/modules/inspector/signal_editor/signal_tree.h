#pragma once

#include <QTreeView>

#include "core/dbc/dbc_message.h"

class SignalTree : public QTreeView {
  Q_OBJECT
 public:
  explicit SignalTree(QWidget* parent);
  void dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight,
                   const QVector<int>& roles = QVector<int>()) override;
  void leaveEvent(QEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;

 signals:
  void highlightRequested(const dbc::Signal* sig);

 private:
  void paintEvent(QPaintEvent* event) override;
  void updateHighlight(const dbc::Signal* sig);

  const dbc::Signal* lastSig_ = nullptr;
};
