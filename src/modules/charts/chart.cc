#include "chart.h"

#include <QApplication>
#include <QGraphicsLayout>
#include <QMenu>
#include <QOpenGLWidget>
#include <QRandomGenerator>
#include <QStyle>

#include "widgets/common.h"

Chart::Chart(QChartView* parent) : parent_(parent), QChart() {
  setBackgroundVisible(false);
  legend()->layout()->setContentsMargins(0, 0, 0, 0);
  legend()->setShowToolTips(true);
  setMargins({0, 0, 0, 0});

  axis_x_ = new QValueAxis(this);
  axis_y_ = new QValueAxis(this);
  addAxis(axis_x_, Qt::AlignBottom);
  addAxis(axis_y_, Qt::AlignLeft);

  initControls();
}

void Chart::syncUI() {
  updateAxisY();
  updateTitle();
  updateSeriesPoints();
  emit resetCache();
}

void Chart::initControls() {
  move_icon_ = new QGraphicsPixmapItem(utils::icon("grip-horizontal"), this);
  move_icon_->setToolTip(tr("Drag and drop to move chart"));

  QToolButton* remove_btn = new ToolButton("x", tr("Remove Chart"));
  close_btn_proxy_ = new QGraphicsProxyWidget(this);
  close_btn_proxy_->setWidget(remove_btn);
  close_btn_proxy_->setZValue(zValue() + 11);

  menu_ = new QMenu();
  // series types
  auto change_series_group = new QActionGroup(menu_);
  change_series_group->setExclusive(true);
  QStringList types{tr("Line"), tr("Step Line"), tr("Scatter")};
  for (int i = 0; i < types.size(); ++i) {
    QAction* act = new QAction(types[i], change_series_group);
    act->setData(i);
    act->setCheckable(true);
    act->setChecked(i == (int)series_type);
    menu_->addAction(act);
  }
  menu_->addSeparator();
  menu_->addAction(tr("Manage Signals"), this, &Chart::manageSignals);
  split_chart_act_ = menu_->addAction(tr("Split Chart"), this, &Chart::splitSeries);

  QToolButton* manage_btn = new ToolButton("menu", "");
  manage_btn->setMenu(menu_);
  manage_btn->setPopupMode(QToolButton::InstantPopup);
  manage_btn->setStyleSheet("QToolButton::menu-indicator { image: none; }");
  manage_btn_proxy_ = new QGraphicsProxyWidget(this);
  manage_btn_proxy_->setWidget(manage_btn);
  manage_btn_proxy_->setZValue(zValue() + 11);

  close_act_ = new QAction(tr("Close"), this);
  connect(close_act_, &QAction::triggered, this, &Chart::close);
  connect(remove_btn, &QToolButton::clicked, close_act_, &QAction::triggered);
  connect(change_series_group, &QActionGroup::triggered, [this](QAction* action) {
    setSeriesType((SeriesType)action->data().toInt());
  });
}

bool Chart::addSignal(const MessageId& msg_id, const dbc::Signal* sig) {
  if (hasSignal(msg_id, sig)) return false;

  QXYSeries* series = createSeries(series_type, sig->color);
  sigs_.emplace_back(msg_id, sig, series);

  updateSeries(sig);
  syncUI();

  emit signalAdded();
  return true;
}

void Chart::takeSignals(std::vector<ChartSignal>&& source_sigs) {
  for (auto& s : source_sigs) {
    if (QChart* old_chart = s.series->chart()) {
      old_chart->removeSeries(s.series);
    }
    attachSeries(s.series);
  }

  std::move(source_sigs.begin(), source_sigs.end(), std::back_inserter(sigs_));
  syncUI();
}

void Chart::removeIf(std::function<bool(const ChartSignal& s)> predicate) {
  int prev_size = sigs_.size();
  for (auto it = sigs_.begin(); it != sigs_.end(); /**/) {
    if (predicate(*it)) {
      removeSeries(it->series);
      it->series->deleteLater();
      it = sigs_.erase(it);
    } else {
      ++it;
    }
  }

  if (sigs_.empty()) {
    emit close();
  } else if (sigs_.size() != prev_size) {
    syncUI();
    emit signalRemoved();
  }
}

void Chart::resizeEvent(QGraphicsSceneResizeEvent* event) {
  QChart::resizeEvent(event);

  qreal left, top, right, bottom;
  layout()->getContentsMargins(&left, &top, &right, &bottom);
  move_icon_->setPos(left, top);
  close_btn_proxy_->setPos(rect().right() - right - close_btn_proxy_->size().width(), top);
  int x = close_btn_proxy_->pos().x() - manage_btn_proxy_->size().width() - style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing);
  manage_btn_proxy_->setPos(x, top);

  if (align_to_ > 0) {
    alignLayout(align_to_, true);
  }
}

void Chart::alignLayout(int left_pos, bool force) {
  if (align_to_ == left_pos && !force) return;

  align_to_ = left_pos;
  qreal left, top, right, bottom;
  layout()->getContentsMargins(&left, &top, &right, &bottom);
  QSizeF legend_size = legend()->layout()->minimumSize();
  legend_size.setWidth(manage_btn_proxy_->sceneBoundingRect().left() - move_icon_->sceneBoundingRect().right());
  legend()->setGeometry({move_icon_->sceneBoundingRect().topRight(), legend_size});

  // add top space for signal value
  QFont signal_value_font;
  signal_value_font.setPointSize(9);
  int adjust_top = legend()->geometry().height() + QFontMetrics(signal_value_font).height() + 3;
  adjust_top = std::max<int>(adjust_top, manage_btn_proxy_->sceneBoundingRect().height() + style()->pixelMetric(QStyle::PM_LayoutTopMargin));
  // add right space for x-axis label
  QSizeF x_label_size = QFontMetrics(axis_x_->labelsFont()).size(Qt::TextSingleLine, QString::number(axis_x_->max(), 'f', 2));
  x_label_size += QSizeF{5, 5};
  setPlotArea(rect().adjusted(left_pos + left, adjust_top + top, -x_label_size.width() / 2 - right, -x_label_size.height() - bottom));
  layout()->invalidate();

  emit resetCache();
}

void Chart::setTheme(QChart::ChartTheme theme) {
  QChart::setTheme(theme);
  auto txtBrush = palette().text();
  if (theme == QChart::ChartThemeDark) {
    axis_x_->setTitleBrush(txtBrush);
    axis_x_->setLabelsBrush(txtBrush);
    axis_y_->setTitleBrush(txtBrush);
    axis_y_->setLabelsBrush(txtBrush);
    legend()->setLabelColor(palette().color(QPalette::Text));
  }
  axis_x_->setLineVisible(false);
  axis_y_->setLineVisible(false);
  for (auto& s : sigs_) {
    s.series->setColor(s.sig->color);
  }
}

void Chart::updateAxisY() {
  if (sigs_.empty()) return;

  double min = std::numeric_limits<double>::max();
  double max = std::numeric_limits<double>::lowest();
  QString unit = sigs_[0].sig->unit;

  for (auto& s : sigs_) {
    if (!s.series->isVisible()) continue;

    // Only show unit when all signals have the same unit
    if (unit != s.sig->unit) {
      unit.clear();
    }

    s.updateRange(axis_x_->min(), axis_x_->max());
    min = std::min(min, s.min);
    max = std::max(max, s.max);
  }
  if (min == std::numeric_limits<double>::max()) min = 0;
  if (max == std::numeric_limits<double>::lowest()) max = 0;

  if (axis_y_->titleText() != unit) {
    axis_y_->setTitleText(unit);
    y_label_width_ = 0;  // recalc width
  }

  double delta = std::abs(max - min) < 1e-3 ? 1 : (max - min) * 0.05;
  auto [min_y, max_y, tick_count] = getNiceAxisNumbers(min - delta, max + delta, 3);
  if (min_y != axis_y_->min() || max_y != axis_y_->max() || y_label_width_ == 0) {
    axis_y_->setRange(min_y, max_y);
    axis_y_->setTickCount(tick_count);

    int n = std::max(int(-std::floor(std::log10((max_y - min_y) / (tick_count - 1)))), 0);
    int max_label_width = 0;
    QFontMetrics fm(axis_y_->labelsFont());
    for (int i = 0; i < tick_count; i++) {
      qreal value = min_y + (i * (max_y - min_y) / (tick_count - 1));
      max_label_width = std::max(max_label_width, fm.horizontalAdvance(QString::number(value, 'f', n)));
    }

    int title_spacing = unit.isEmpty() ? 0 : QFontMetrics(axis_y_->titleFont()).size(Qt::TextSingleLine, unit).height();
    y_label_width_ = title_spacing + max_label_width + 15;
    axis_y_->setLabelFormat(QString("%.%1f").arg(n));
    emit axisYLabelWidthChanged(y_label_width_);
  }
}

void Chart::updateTitle() {
  // Use CSS to draw titles in the WindowText color
  auto tmp = palette().color(QPalette::WindowText);
  auto titleColorCss = tmp.name(QColor::HexArgb);
  // Draw message details in similar color, but slightly fade it to the background
  tmp.setAlpha(180);
  auto msgColorCss = tmp.name(QColor::HexArgb);

  for (auto& s : sigs_) {
    auto decoration = s.series->isVisible() ? "none" : "line-through";
    QString name = QString("<span style=\"text-decoration:%1; color:%2\"><b>%3</b> <font color=\"%4\">%5 %6</font></span>")
                       .arg(decoration, titleColorCss, s.sig->name,
                            msgColorCss, msgName(s.msg_id), s.msg_id.toString());
    if (s.series->name() != name) {
      s.series->setName(name);
    }
  }
  split_chart_act_->setEnabled(sigs_.size() > 1);
}

void Chart::onMarkerClicked() {
  auto marker = qobject_cast<QLegendMarker*>(sender());
  Q_ASSERT(marker);
  if (sigs_.size() > 1) {
    auto series = marker->series();
    series->setVisible(!series->isVisible());
    marker->setVisible(true);
    syncUI();
  }
}

void Chart::updateSeriesPoints() {
  // Show points when zoomed in enough
  for (auto& s : sigs_) {
    auto begin = std::lower_bound(s.vals.cbegin(), s.vals.cend(), axis_x_->min(), xLessThan);
    auto end = std::lower_bound(begin, s.vals.cend(), axis_x_->max(), xLessThan);
    if (begin != end) {
      int num_points = std::max<int>((end - begin), 1);
      QPointF right_pt = end == s.vals.cend() ? s.vals.back() : *end;
      double pixels_per_point = (mapToPosition(right_pt).x() - mapToPosition(*begin).x()) / num_points;

      if (series_type == SeriesType::Scatter) {
        qreal size = std::clamp(pixels_per_point / 2.0, 2.0, 8.0);
        if (s.series->useOpenGL()) {
          size *= qApp->devicePixelRatio();
        }
        ((QScatterSeries*)s.series)->setMarkerSize(size);
      } else {
        s.series->setPointsVisible(num_points == 1 || pixels_per_point > 20);
      }
    }
  }
}

void Chart::setSeriesType(SeriesType type) {
  if (type != series_type) {
    series_type = type;
    for (auto& s : sigs_) {
      removeSeries(s.series);
      s.series->deleteLater();
    }
    for (auto& s : sigs_) {
      s.series = createSeries(series_type, s.sig->color);
      const auto& points = series_type == SeriesType::StepLine ? s.step_vals : s.vals;
      s.series->replace(QVector<QPointF>(points.cbegin(), points.cend()));
    }
    syncUI();

    menu_->actions()[(int)type]->setChecked(true);
  }
}

QXYSeries* Chart::createSeries(SeriesType type, QColor color) {
  QXYSeries* series = nullptr;
  if (type == SeriesType::Line) {
    series = new QLineSeries(this);
    legend()->setMarkerShape(QLegend::MarkerShapeRectangle);
  } else if (type == SeriesType::StepLine) {
    series = new QLineSeries(this);
    legend()->setMarkerShape(QLegend::MarkerShapeFromSeries);
  } else {
    series = new QScatterSeries(this);
    static_cast<QScatterSeries*>(series)->setBorderColor(color);
    legend()->setMarkerShape(QLegend::MarkerShapeCircle);
  }
  series->setColor(color);
  // TODO: Due to a bug in CameraWidget the camera frames
  // are drawn instead of the graphs on MacOS. Re-enable OpenGL when fixed
#ifndef __APPLE__
  series->setUseOpenGL(true);
  // Qt doesn't properly apply device pixel ratio in OpenGL mode
  QPen pen = series->pen();
  pen.setWidthF(2.0 * qApp->devicePixelRatio());
  series->setPen(pen);
#endif
  attachSeries(series);
  return series;
}

void Chart::attachSeries(QXYSeries* series) {
  setSeriesColor(series, series->color());
  addSeries(series);
  series->attachAxis(axis_x_);
  series->attachAxis(axis_y_);

  for (QLegendMarker* marker : legend()->markers(series)) {
    connect(marker, &QLegendMarker::clicked, this, &Chart::onMarkerClicked, Qt::UniqueConnection);
  }

  // disables the delivery of mouse events to the opengl widget.
  // this enables the user to select the zoom area when the mouse press on the data point.
  auto glwidget = parent_->findChild<QOpenGLWidget*>();
  if (glwidget && !glwidget->testAttribute(Qt::WA_TransparentForMouseEvents)) {
    glwidget->setAttribute(Qt::WA_TransparentForMouseEvents);
  }
}

void Chart::setSeriesColor(QXYSeries* series, QColor color) {
  auto existing_series = this->series();
  for (auto s : existing_series) {
    if (s != series && std::abs(color.hueF() - qobject_cast<QXYSeries*>(s)->color().hueF()) < 0.1) {
      // use different color to distinguish it from others.
      auto last_color = qobject_cast<QXYSeries*>(existing_series.back())->color();
      color.setHsvF(std::fmod(last_color.hueF() + 60 / 360.0, 1.0),
                    QRandomGenerator::global()->bounded(35, 100) / 100.0,
                    QRandomGenerator::global()->bounded(85, 100) / 100.0);
      break;
    }
  }
  series->setColor(color);
}

void Chart::updateSeries(const dbc::Signal* sig, const MessageEventsMap* msg_new_events) {
  for (auto& s : sigs_) {
    if (!sig || s.sig == sig) {
      s.updateSeries(series_type, msg_new_events);
    }
  }
  updateAxisY();
  // invoke resetChartCache in ui thread
  QMetaObject::invokeMethod(this, &Chart::resetCache, Qt::QueuedConnection);
}

void Chart::handleSignalChange(const dbc::Signal* sig) {
  auto it = std::find_if(sigs_.begin(), sigs_.end(), [sig](auto& s) { return s.sig == sig; });
  if (it != sigs_.end()) {
    if (it->series->color() != sig->color) {
      setSeriesColor(it->series, sig->color);
    }
    syncUI();
  }
}

void Chart::msgUpdated(MessageId id) {
  if (std::any_of(sigs_.cbegin(), sigs_.cend(), [=](auto& s) { return s.msg_id.address == id.address; })) {
    updateTitle();
  }
}

bool Chart::updateAxisXRange(double min, double max) {
  if (min != axis_x_->min() || max != axis_x_->max()) {
    axis_x_->setRange(min, max);
    syncUI();
    return true;
  }
  return false;
}

double Chart::getTooltipTextAt(double sec, QStringList& text_list) {
  double x = -1;
  for (auto& s : sigs_) {
    if (s.series->isVisible()) {
      QString value = "--";
      // use reverse iterator to find last item <= sec.
      auto it = std::lower_bound(s.vals.crbegin(), s.vals.crend(), sec, [](auto& p, double x) { return p.x() > x; });
      if (it != s.vals.crend() && it->x() >= axis_x_->min()) {
        value = s.sig->formatValue(it->y(), false);
        s.track_pt = *it;
        x = std::max(x, mapToPosition(*it).x());
      }
      QString name = sigs_.size() > 1 ? s.sig->name + ": " : "";
      QString min = s.min == std::numeric_limits<double>::max() ? "--" : QString::number(s.min);
      QString max = s.max == std::numeric_limits<double>::lowest() ? "--" : QString::number(s.max);
      text_list << QString("<span style=\"color:%1;\">■ </span>%2<b>%3</b> (%4, %5)")
                       .arg(s.series->color().name(), name, value, min, max);
    }
  }
  return x;
}
