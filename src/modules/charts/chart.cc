#include "chart.h"

#include <QApplication>
#include <QGraphicsLayout>
#include <QMenu>
#include <QOpenGLWidget>
#include <QRandomGenerator>
#include <QStyle>

#include "widgets/common.h"
#include "widgets/tool_button.h"

Chart::Chart(QChartView* parent) : parent_(parent), QChart() {
  setBackgroundVisible(false);
  setMargins({0, 0, 0, 0});
  layout()->setContentsMargins(4, 2, 4, 2);
  legend()->layout()->setContentsMargins(0, 0, 0, 0);
  legend()->setShowToolTips(true);

  axis_x_ = new QValueAxis(this);
  axis_y_ = new QValueAxis(this);
  addAxis(axis_x_, Qt::AlignBottom);
  addAxis(axis_y_, Qt::AlignLeft);

  initControls();
  setupConnections();
}

void Chart::setupConnections() {
  connect(axis_x_, &QValueAxis::rangeChanged, this, &Chart::updateAxisY);
  connect(axis_x_, &QValueAxis::rangeChanged, this, &Chart::resetCache);
  connect(axis_y_, &QValueAxis::rangeChanged, this, &Chart::resetCache);
  connect(axis_y_, &QAbstractAxis::titleTextChanged, this, &Chart::resetCache);
}

void Chart::syncUI() {
  updateAxisY();
  updateTitle();
  updateSeriesPoints();
  emit resetCache();
}

void Chart::initControls() {
  const int icon_size = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
  move_icon_ = new QGraphicsPixmapItem(utils::icon("grip-horizontal", QSize(icon_size, icon_size)), this);
  move_icon_->setToolTip(tr("Drag to reorder or merge"));

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
  menu_->addAction(tr("Edit Signals..."), this, &Chart::manageSignals);
  split_chart_act_ = menu_->addAction(tr("Split Signals"), this, &Chart::splitSeries);

  QToolButton* manage_btn = new ToolButton("menu", "");
  manage_btn->setToolTip(tr("Chart Settings"));
  manage_btn->setMenu(menu_);
  manage_btn->setPopupMode(QToolButton::InstantPopup);
  manage_btn_proxy_ = new QGraphicsProxyWidget(this);
  manage_btn_proxy_->setWidget(manage_btn);
  manage_btn_proxy_->setZValue(zValue() + 11);

  const QString clean_style = R"(
    QToolButton, QToolButton:hover, QToolButton:pressed, QToolButton:checked {
      background: transparent;
      border: none;
      padding: 0px;
    }
    QToolButton::menu-indicator {
      image: none;
    }
  )";
  remove_btn->setStyleSheet(clean_style);
  remove_btn->setFocusPolicy(Qt::NoFocus);
  manage_btn->setStyleSheet(clean_style);
  manage_btn->setFocusPolicy(Qt::NoFocus);

  close_act_ = new QAction(tr("Remove Chart"), this);
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

  prepareData(sig);
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
  int x = close_btn_proxy_->pos().x() - manage_btn_proxy_->size().width() - style()->pixelMetric(QStyle::PM_ToolBarItemSpacing);
  manage_btn_proxy_->setPos(x, top);

  if (align_to_ > 0) {
    alignLayout(align_to_, true);
  }
}

void Chart::alignLayout(int left_pos, bool force) {
  if (align_to_ == left_pos && !force) return;
  align_to_ = left_pos;

  const QRectF move_rect = move_icon_->sceneBoundingRect();
  const QRectF manage_rect = manage_btn_proxy_->sceneBoundingRect();

  // Stretch legend to fill the gap between the move and manage icons
  // Ensure legend height matches the larger UI controls (manage_btn) rather than the
  // smaller move_icon to provide sufficient top margin for signal value labels.
  QRectF legend_geom(move_rect.topRight(), QSize(manage_rect.left() - move_rect.right(), manage_rect.height()));
  legend()->setGeometry(legend_geom);

  QFontMetrics fm_x(axis_x_->labelsFont());
  int x_label_h = fm_x.height();

  // Top padding: Legend height + Signal value height (estimated by axis font height)
  int adjust_top = std::max((int)legend_geom.height() + x_label_h + 3,
                            (int)manage_rect.height() + style()->pixelMetric(QStyle::PM_LayoutTopMargin));

  // Right padding: Half of the last X-axis label width to prevent clipping
  int x_label_half_w = fm_x.horizontalAdvance(QString::number(axis_x_->max(), 'f', 2)) / 2;

  // Update Plot Area with adjusted margins
  qreal l, t, r, b;
  layout()->getContentsMargins(&l, &t, &r, &b);

  // Note: 'left_pos' already includes the calculated Y-axis label width from updateAxisY
  setPlotArea(rect().adjusted(left_pos + l, adjust_top + t, -x_label_half_w - 5 - r, -x_label_h - 5 - b));

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

  QString unit;
  auto [g_min, g_max] = calculateValueRange(unit);

  if (axis_y_->titleText() != unit) {
    axis_y_->setTitleText(unit);
    y_label_width_ = 0; // Force recalculation of margin
  }

  double delta = std::abs(g_max - g_min) < 1e-3 ? 1 : (g_max - g_min) * 0.05;
  auto [min_y, max_y, ticks] = getNiceAxisNumbers(g_min - delta, g_max + delta, 3);
  bool range_changed = !qFuzzyCompare(min_y, axis_y_->min()) ||
                       !qFuzzyCompare(max_y, axis_y_->max()) ||
                       ticks != axis_y_->tickCount();

  if (!range_changed && y_label_width_ != 0) return;

  axis_y_->setRange(min_y, max_y);
  axis_y_->setTickCount(ticks);

  updateYLabelWidth(min_y, max_y, ticks, unit);
}

std::pair<double, double> Chart::calculateValueRange(QString& common_unit) {
  double g_min = std::numeric_limits<double>::max();
  double g_max = std::numeric_limits<double>::lowest();

  const double x_min = axis_x_->min();
  const double x_max = axis_x_->max();

  if (!sigs_.empty()) common_unit = sigs_[0].sig->unit;

  for (auto& s : sigs_) {
    if (!s.series->isVisible()) continue;

    s.updateRange(x_min, x_max);
    g_min = std::min(g_min, s.min_value);
    g_max = std::max(g_max, s.max_value);

    if (common_unit != s.sig->unit) common_unit.clear();
  }

  // Fallback for no data or flat line
  if (g_min > g_max) return {0.0, 1.0};
  return {g_min, g_max};
}

void Chart::updateYLabelWidth(double min_y, double max_y, int tick_count, const QString& unit) {
  double step = (max_y - min_y) / std::max(1, tick_count - 1);
  int precision = std::clamp(static_cast<int>(-std::floor(std::log10(step))), 0, 6);
  axis_y_->setLabelFormat(QString("%.%1f").arg(precision));

  QFontMetrics fm(axis_y_->labelsFont());
  int max_label_width = 0;
  for (int i = 0; i < tick_count; i++) {
    double val = min_y + (i * step);
    max_label_width = std::max(max_label_width, fm.horizontalAdvance(QString::number(val, 'f', precision)));
  }

  int title_spacing = unit.isEmpty() ? 0 : QFontMetrics(axis_y_->titleFont()).size(Qt::TextSingleLine, unit).height();
  int new_width = title_spacing + max_label_width + 15;
  if (std::abs(new_width - y_label_width_) > 2 || y_label_width_ == 0) {
    y_label_width_ = new_width;
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
  const double range_sec = axis_x_->max() - axis_x_->min();
  const double plot_width = plotArea().width();
  if (range_sec <= 0 || plot_width <= 0) return;

  const double sec_per_px = range_sec / plot_width;
  const qreal dpr = qApp->devicePixelRatio();  // Get once

  for (auto& s : sigs_) {
    if (s.vals.size() < 2) continue;

    // Average time between data points
    double avg_period = (s.vals.back().x() - s.vals.front().x()) / s.vals.size();

    if (series_type == SeriesType::Scatter) {
      // Scale dot size by DPR so it looks consistent on all screens
      qreal size = std::clamp(avg_period / sec_per_px / 2.0, 2.0, 8.0);
      static_cast<QScatterSeries*>(s.series)->setMarkerSize(size * dpr);
    } else {
      // Threshold: Hide points if they are closer than 15 logical pixels
      s.series->setPointsVisible(avg_period > (sec_per_px * 15.0));
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

void Chart::prepareData(const dbc::Signal* sig, const MessageEventsMap* msg_new_events) {
  double min_x = axis_x_->min();
  double max_x = axis_x_->max();
  for (auto& s : sigs_) {
    if (!sig || s.sig == sig) {
      s.prepareData(msg_new_events, min_x, max_x);
    }
  }
}

void Chart::updateSeries(const dbc::Signal* sig) {
  for (auto& s : sigs_) {
    if (!sig || s.sig == sig) {
      s.updateSeries(series_type);
    }
  }
  updateAxisY();
  resetCache();
}

void Chart::handleSignalChange(const dbc::Signal* sig) {
  auto it = std::find_if(sigs_.begin(), sigs_.end(), [sig](auto& s) { return s.sig == sig; });
  if (it != sigs_.end()) {
    if (it->series->color() != sig->color) {
      setSeriesColor(it->series, sig->color);
    }
    prepareData(sig);
    updateSeries();
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
      QString min = s.min_value == std::numeric_limits<double>::max() ? "--" : QString::number(s.min_value);
      QString max = s.max_value == std::numeric_limits<double>::lowest() ? "--" : QString::number(s.max_value);
      text_list << QString("<span style=\"color:%1;\">■ </span>%2<b>%3</b> (%4, %5)")
                       .arg(s.series->color().name(), name, value, min, max);
    }
  }
  return x;
}
