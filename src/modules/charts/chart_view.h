#pragma once

#include <QtCharts/QChartView>
#include <tuple>
#include <utility>
#include <vector>

#include "chart.h"
#include "tiplabel.h"

const QString CHART_MIME_TYPE = "application/x-cabanachartview";
const int CHART_MIN_WIDTH = 300;

class ChartsPanel;

class ChartView : public QChartView {
  Q_OBJECT

 public:
  ChartView(const std::pair<double, double>& x_range, ChartsPanel* parent = nullptr);
  inline Chart* chart() { return chart_; }
  inline bool empty() const { return chart_->sigs_.empty(); }
  void updatePlot(double cur, double min, double max);
  void showCursor(double sec, const QRect& visible_rect);
  void hideCursor();
  void startAnimation();
  void resetChartCache();
  double secondsAtPoint(const QPointF& pt) const { return chart_->mapToValue(pt).x(); }

 signals:
  void axisYLabelWidthChanged(int w);

 private slots:
  void manageSignals();
  void msgRemoved(MessageId id) {
    chart_->removeIf([=](auto& s) { return s.msg_id.address == id.address && !GetDBC()->msg(id); });
  }
  void signalRemoved(const dbc::Signal* sig) {
    chart_->removeIf([=](auto& s) { return s.sig == sig; });
  }

 private:
  void setupConnections();
  void contextMenuEvent(QContextMenuEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* ev) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  QSize sizeHint() const override;
  void paintEvent(QPaintEvent* event) override;
  void drawForeground(QPainter* painter, const QRectF& rect) override;
  void drawBackground(QPainter* painter, const QRectF& rect) override;
  void drawSignalValue(QPainter* painter);
  void drawTimeline(QPainter* painter);
  void drawRubberBandTimeRange(QPainter* painter);
  void handleDragStart();
  void updateCache();
  inline void clearTrackPoints() {
    for (auto& s : chart_->sigs_) s.track_pt = {};
  }

  TipLabel* tip_label;

  double cur_sec = 0;
  bool is_scrubbing = false;
  bool resume_after_scrub = false;
  QPixmap chart_pixmap;
  QFont signal_value_font;
  ChartsPanel* charts_panel;

  Chart* chart_ = nullptr;
};
