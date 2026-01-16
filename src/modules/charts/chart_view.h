#pragma once

#include <tuple>
#include <utility>
#include <vector>

#include "chart.h"
#include "tiplabel.h"

using namespace QtCharts;

const QString CHART_MIME_TYPE = "application/x-cabanachartview";
const int CHART_MIN_WIDTH = 300;

class ChartsPanel;

class ChartView : public QChartView {
  Q_OBJECT

public:
  ChartView(const std::pair<double, double> &x_range, ChartsPanel *parent = nullptr);
  void updatePlot(double cur, double min, double max);
  void showTip(double sec);
  void hideTip();
  void startAnimation();
  inline Chart* chart() const { return chart_; }
  double secondsAtPoint(const QPointF &pt) const { return chart()->mapToValue(pt).x(); }

signals:
  void axisYLabelWidthChanged(int w);

private slots:
  void manageSignals();
  void msgRemoved(MessageId id) { chart_->removeIf([=](auto &s) { return s.msg_id.address == id.address && !GetDBC()->msg(id); }); }
  void signalRemoved(const dbc::Signal *sig) { chart_->removeIf([=](auto &s) { return s.sig == sig; }); }

private:
  void setupConnections();
  void contextMenuEvent(QContextMenuEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *ev) override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragLeaveEvent(QDragLeaveEvent *event) override { drawDropIndicator(false); }
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  QSize sizeHint() const override;
  void resetChartCache();
  void paintEvent(QPaintEvent *event) override;
  void drawForeground(QPainter *painter, const QRectF &rect) override;
  void drawBackground(QPainter *painter, const QRectF &rect) override;
  void drawDropIndicator(bool draw) { if (std::exchange(can_drop, draw) != can_drop) viewport()->update(); }
  void drawSignalValue(QPainter *painter);
  void drawTimeline(QPainter *painter);
  void drawRubberBandTimeRange(QPainter *painter);
  inline void clearTrackPoints() { for (auto &s : chart_->sigs_) s.track_pt = {}; }

  TipLabel *tip_label;

  double cur_sec = 0;
  bool is_scrubbing = false;
  bool resume_after_scrub = false;
  QPixmap chart_pixmap;
  bool can_drop = false;
  double tooltip_x = -1;
  QFont signal_value_font;
  ChartsPanel *charts_widget;

  Chart *chart_ = nullptr;
  friend class ChartsPanel;
};
