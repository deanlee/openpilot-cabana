#pragma once

#include <QGraphicsPixmapItem>
#include <QGraphicsProxyWidget>
#include <QtCharts/QChartView>
#include <QtCharts/QLegendMarker>

#include "chart_signal.h"

using namespace QtCharts;

class Chart : public QChart {
  Q_OBJECT
 public:
  Chart(QChartView* parent);
  void alignLayout(int left_pos, bool force = false);
  void setTheme(QChart::ChartTheme theme);
  void removeIf(std::function<bool(const ChartSignal& s)> predicate);
  bool updateAxisXRange(double min, double max);
  void handleSignalChange(const dbc::Signal* sig);
  bool addSignal(const MessageId& msg_id, const dbc::Signal* sig);
  void takeSignals(std::vector<ChartSignal>&& source_sigs);
  double getTooltipTextAt(double sec, QStringList& text_list);
  void msgUpdated(MessageId id);
  void syncUI();
  void updateSeries(const dbc::Signal* sig = nullptr, const MessageEventsMap* msg_new_events = nullptr);
  inline bool hasSignal(const MessageId& msg_id, const dbc::Signal* sig) const {
    return std::any_of(sigs_.cbegin(), sigs_.cend(), [&](auto& s) { return s.msg_id == msg_id && s.sig == sig; });
  }
  void setSeriesType(SeriesType type);

 signals:
  void signalAdded();
  void signalRemoved();
  void manageSignals();
  void splitSeries();
  void close();
  void axisYLabelWidthChanged(int w);
  void resetCache();

 protected:
  void attachSeries(QXYSeries* series);
  void updateTitle();
  void initControls();
  void onMarkerClicked();
  void resizeEvent(QGraphicsSceneResizeEvent* event) override;
  void setSeriesColor(QXYSeries* series, QColor color);
  void updateSeriesPoints();
  void updateAxisY();
  QXYSeries* createSeries(SeriesType type, QColor color);

 public:
  int y_label_width_ = 0;
  SeriesType series_type = SeriesType::Line;
  QValueAxis* axis_x_;
  QValueAxis* axis_y_;

  std::vector<ChartSignal> sigs_;
  QMenu* menu_;

  QAction* close_act_;
  QGraphicsPixmapItem* move_icon_;

 private:
  int align_to_ = 0;
  QAction* split_chart_act_;
  QGraphicsProxyWidget* close_btn_proxy_;
  QGraphicsProxyWidget* manage_btn_proxy_;
  QChartView* parent_ = nullptr;
};
