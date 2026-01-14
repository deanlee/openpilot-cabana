#pragma once

#include <tuple>
#include <utility>
#include <vector>

#include <QMenu>
#include <QGraphicsPixmapItem>
#include <QGraphicsProxyWidget>
#include <QtCharts/QChartView>
#include <QtCharts/QLegendMarker>

#include "chart_signal.h"
#include "tiplabel.h"

using namespace QtCharts;
class ChartsPanel;

class ChartView : public QChartView {
  Q_OBJECT

public:
  ChartView(const std::pair<double, double> &x_range, ChartsPanel *parent = nullptr);
  void addSignal(const MessageId &msg_id, const dbc::Signal *sig);
  bool hasSignal(const MessageId &msg_id, const dbc::Signal *sig) const;
  void updateSeries(const dbc::Signal *sig = nullptr, const MessageEventsMap *msg_new_events = nullptr);
  void updatePlot(double cur, double min, double max);
  void setSeriesType(SeriesType type);
  void updatePlotArea(int left, bool force = false);
  void showTip(double sec);
  void hideTip();
  void startAnimation();
  double secondsAtPoint(const QPointF &pt) const { return chart()->mapToValue(pt).x(); }

signals:
  void axisYLabelWidthChanged(int w);

private slots:
  void signalUpdated(const dbc::Signal *sig);
  void manageSignals();
  void handleMarkerClicked();
  void msgUpdated(MessageId id);
  void msgRemoved(MessageId id) { removeIf([=](auto &s) { return s.msg_id.address == id.address && !GetDBC()->msg(id); }); }
  void signalRemoved(const dbc::Signal *sig) { removeIf([=](auto &s) { return s.sig == sig; }); }

private:
  void setupConnections();
  void createToolButtons();
  void addSeries(QXYSeries *series);
  void contextMenuEvent(QContextMenuEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *ev) override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragLeaveEvent(QDragLeaveEvent *event) override { drawDropIndicator(false); }
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  QSize sizeHint() const override;
  void updateAxisY();
  void updateTitle();
  void resetChartCache();
  void setTheme(QChart::ChartTheme theme);
  void paintEvent(QPaintEvent *event) override;
  void drawForeground(QPainter *painter, const QRectF &rect) override;
  void drawBackground(QPainter *painter, const QRectF &rect) override;
  void drawDropIndicator(bool draw) { if (std::exchange(can_drop, draw) != can_drop) viewport()->update(); }
  void drawSignalValue(QPainter *painter);
  void drawTimeline(QPainter *painter);
  void drawRubberBandTimeRange(QPainter *painter);
  QXYSeries *createSeries(SeriesType type, QColor color);
  void setSeriesColor(QXYSeries *, QColor color);
  void updateSeriesPoints();
  void removeIf(std::function<bool(const ChartSignal &)> predicate);
  inline void clearTrackPoints() { for (auto &s : sigs) s.track_pt = {}; }

  int y_label_width = 0;
  int align_to = 0;
  QValueAxis *axis_x;
  QValueAxis *axis_y;
  QMenu *menu;
  QAction *split_chart_act;
  QAction *close_act;
  QGraphicsPixmapItem *move_icon;
  QGraphicsProxyWidget *close_btn_proxy;
  QGraphicsProxyWidget *manage_btn_proxy;
  TipLabel *tip_label;
  std::vector<ChartSignal> sigs;
  double cur_sec = 0;
  SeriesType series_type = SeriesType::Line;
  bool is_scrubbing = false;
  bool resume_after_scrub = false;
  QPixmap chart_pixmap;
  bool can_drop = false;
  double tooltip_x = -1;
  QFont signal_value_font;
  ChartsPanel *charts_widget;
  friend class ChartsPanel;
};
