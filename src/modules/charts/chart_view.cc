#include "chart_view.h"

#include <QActionGroup>
#include <QApplication>
#include <QDrag>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsItemGroup>
#include <QGraphicsLayout>
#include <QGraphicsOpacityEffect>
#include <QMenu>
#include <QMimeData>
#include <QOpenGLWidget>
#include <QPropertyAnimation>
#include <QRubberBand>
#include <QScreen>
#include <QWindow>
#include <algorithm>
#include <limits>

#include "charts_panel.h"
#include "modules/settings/settings.h"
#include "modules/system/stream_manager.h"

// ChartAxisElement's padding is 4 (https://codebrowser.dev/qt5/qtcharts/src/charts/axis/chartaxiselement_p.h.html)
const int AXIS_X_TOP_MARGIN = 4;
const double MIN_ZOOM_SECONDS = 0.01; // 10ms

ChartView::ChartView(const std::pair<double, double>& x_range, ChartsPanel* parent)
    : QChartView(parent), charts_panel(parent) {
  setRubberBand(QChartView::HorizontalRubberBand);
  setMouseTracking(true);
  setFixedHeight(settings.chart_height);
  setMinimumWidth(CHART_MIN_WIDTH);
  setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);

  chart_ = new Chart(this);
  setChart(chart_);
  chart_->series_type = (SeriesType)settings.chart_series_type;
  chart_->axis_x_->setRange(x_range.first, x_range.second);
  chart_->setTheme(utils::isDarkTheme() ? QChart::QChart::ChartThemeDark : QChart::ChartThemeLight);

  tip_label = new TipLabel(this);

  signal_value_font.setPointSize(9);

  setupConnections();
}

void ChartView::setupConnections() {
  connect(chart_, &Chart::axisYLabelWidthChanged, this, &ChartView::axisYLabelWidthChanged);
  connect(chart_, &Chart::signalAdded, charts_panel, &ChartsPanel::seriesChanged);
  connect(chart_, &Chart::signalRemoved, charts_panel, &ChartsPanel::seriesChanged);
  connect(chart_, &Chart::manageSignals, this, &ChartView::manageSignals);
  connect(chart_, &Chart::splitSeries, [this]() { charts_panel->splitChart(this); });
  connect(chart_, &Chart::close, [this]() { charts_panel->removeChart(this); });
  connect(chart_, &Chart::resetCache, this, &ChartView::resetChartCache);

  connect(window()->windowHandle(), &QWindow::screenChanged, this, &ChartView::resetChartCache);

  connect(GetDBC(), &dbc::Manager::signalRemoved, this, &ChartView::signalRemoved);
  connect(GetDBC(), &dbc::Manager::signalUpdated, chart_, &Chart::handleSignalChange);
  connect(GetDBC(), &dbc::Manager::msgRemoved, this, &ChartView::msgRemoved);
  connect(GetDBC(), &dbc::Manager::msgUpdated, chart_, &Chart::msgUpdated);
}

QSize ChartView::sizeHint() const {
  return {CHART_MIN_WIDTH, settings.chart_height};
}

void ChartView::manageSignals() {
  SignalPicker dlg(tr("Manage Chart"), this);
  for (auto &s : chart_->sigs_) {
    dlg.addSelected(s.msg_id, s.sig);
  }
  if (dlg.exec() == QDialog::Accepted) {
    auto items = dlg.seletedItems();
    for (auto s : items) {
      chart_->addSignal(s->msg_id, s->sig);
    }
    chart_->removeIf([&](auto &s) {
      return std::none_of(items.cbegin(), items.cend(), [&](auto &it) { return s.msg_id == it->msg_id && s.sig == it->sig; });
    });
  }
}

void ChartView::updatePlot(double cur, double min, double max) {
  cur_sec = cur;
  chart_->updateAxisXRange(min, max);
  viewport()->update();
}

QPixmap getBlankShadowPixmap(const QPixmap &px, int radius) {
  QGraphicsDropShadowEffect *e = new QGraphicsDropShadowEffect;
  e->setColor(QColor(40, 40, 40, 245));
  e->setOffset(0, 0);
  e->setBlurRadius(radius);

  qreal dpr = px.devicePixelRatio();
  QPixmap blank(px.size());
  blank.setDevicePixelRatio(dpr);
  blank.fill(Qt::white);

  QGraphicsScene scene;
  QGraphicsPixmapItem item(blank);
  item.setGraphicsEffect(e);
  scene.addItem(&item);

  QPixmap shadow(px.size() + QSize(radius * dpr * 2, radius * dpr * 2));
  shadow.setDevicePixelRatio(dpr);
  shadow.fill(Qt::transparent);
  QPainter p(&shadow);
  scene.render(&p, {QPoint(), shadow.size() / dpr}, item.boundingRect().adjusted(-radius, -radius, radius, radius));
  return shadow;
}

static QPixmap getDropPixmap(const QPixmap &src) {
  static QPixmap shadow_px;
  const int radius = 10;
  if (shadow_px.size() != src.size() + QSize(radius * 2, radius * 2)) {
    shadow_px = getBlankShadowPixmap(src, radius);
  }
  QPixmap px = shadow_px;
  QPainter p(&px);
  QRectF target_rect(QPointF(radius, radius), src.size() / src.devicePixelRatio());
  p.drawPixmap(target_rect.topLeft(), src);
  p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
  p.fillRect(target_rect, QColor(0, 0, 0, 200));
  return px;
}

void ChartView::contextMenuEvent(QContextMenuEvent *event) {
  QMenu context_menu(this);
  context_menu.addActions(chart_->menu_->actions());
  context_menu.addSeparator();
  // context_menu.addAction(charts_panel->toolbar->undo_zoom_action);
  // context_menu.addAction(charts_panel->toolbar->redo_zoom_action);
  context_menu.addSeparator();
  context_menu.addAction(chart_->close_act_);
  context_menu.exec(event->globalPos());
}

void ChartView::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton && chart_->move_icon_->sceneBoundingRect().contains(event->pos())) {
    handlDragStart();
  } else if (event->button() == Qt::LeftButton && QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier)) {
    // Save current playback state when scrubbing
    resume_after_scrub = !StreamManager::stream()->isPaused();
    if (resume_after_scrub) {
      StreamManager::stream()->pause(true);
    }
    is_scrubbing = true;
  } else {
    QChartView::mousePressEvent(event);
  }
}

void ChartView::mouseReleaseEvent(QMouseEvent *event) {
  auto rubber = findChild<QRubberBand *>();
  if (event->button() == Qt::LeftButton && rubber && rubber->isVisible()) {
    rubber->hide();
    auto rect = rubber->geometry().normalized();
    // Prevent zooming/seeking past the end of the route
    auto *can = StreamManager::stream();
    double min = std::clamp(secondsAtPoint(rect.topLeft()), can->minSeconds(), can->maxSeconds());
    double max = std::clamp(secondsAtPoint(rect.bottomRight()), can->minSeconds(), can->maxSeconds());
    if (rubber->width() <= 0) {
      // no rubber dragged, seek to mouse position
      can->seekTo(min);
    } else if (rubber->width() > 10 && (max - min) > MIN_ZOOM_SECONDS) {
      charts_panel->toolbar->zoom_undo_stack->push(new ZoomCommand({min, max}));
    } else {
      viewport()->update();
    }
    event->accept();
  } else if (event->button() == Qt::RightButton) {
    charts_panel->toolbar->zoom_undo_stack->undo();
    event->accept();
  } else {
    QGraphicsView::mouseReleaseEvent(event);
  }

  // Resume playback if we were scrubbing
  is_scrubbing = false;
  if (resume_after_scrub) {
    StreamManager::stream()->pause(false);
    resume_after_scrub = false;
  }
}

void ChartView::mouseMoveEvent(QMouseEvent *ev) {
  const auto plot_area = chart_->plotArea();
  // Scrubbing
  if (is_scrubbing && QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier)) {
    if (plot_area.contains(ev->pos())) {
      auto *can = StreamManager::stream();
      can->seekTo(std::clamp(secondsAtPoint(ev->pos()), can->minSeconds(), can->maxSeconds()));
    }
  }

  auto rubber = findChild<QRubberBand *>();
  bool is_zooming = rubber && rubber->isVisible();
  clearTrackPoints();

  if (!is_zooming && plot_area.contains(ev->pos()) && isActiveWindow()) {
    charts_panel->updateHover(secondsAtPoint(ev->pos()));
  } else if (tip_label->isVisible()) {
    charts_panel->hideHover();
  }

  QChartView::mouseMoveEvent(ev);
  if (is_zooming) {
    QRect rubber_rect = rubber->geometry();
    rubber_rect.setLeft(std::max(rubber_rect.left(), (int)plot_area.left()));
    rubber_rect.setRight(std::min(rubber_rect.right(), (int)plot_area.right()));
    if (rubber_rect != rubber->geometry()) {
      rubber->setGeometry(rubber_rect);
    }
    viewport()->update();
  }
}

void ChartView::handlDragStart() {
  QMimeData* mimeData = new QMimeData;
  mimeData->setData(CHART_MIME_TYPE, QByteArray::number((qulonglong)this));
  QPixmap px = grab().scaledToWidth(CHART_MIN_WIDTH * viewport()->devicePixelRatio(), Qt::SmoothTransformation);
  QDrag* drag = new QDrag(this);
  drag->setMimeData(mimeData);
  drag->setPixmap(getDropPixmap(px));
  drag->setHotSpot(-QPoint(5, 5));

  drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::MoveAction);

  // Reset any drag state in the container
  if (auto* container = qobject_cast<ChartsContainer*>(this->parentWidget())) {
    container->resetDragState();
  }
}

void ChartView::showCursor(double sec, const QRect& visible_rect) {
  int tooltip_x = chart_->mapToPosition({sec, 0}).x();

  QStringList entries;
  int x_override = chart_->getTooltipTextAt(sec, entries);
  int x = (x_override < 0) ? tooltip_x : x_override;

  // Use the HTML table format for better alignment
  QString text = QString("<b>%1s</b><br/>").arg(secondsAtPoint({(qreal)x, 0}), 0, 'f', 3);
  text += entries.join("<br/>");

  QPoint pt(x, chart_->plotArea().top());
  tip_label->showText(pt, text, this, visible_rect);
  viewport()->update();
}

void ChartView::hideCursor() {
  if (!tip_label->isVisible()) return;

  clearTrackPoints();
  tip_label->hide();
  viewport()->update();
}

void ChartView::dragEnterEvent(QDragEnterEvent *event) {
  if (event->mimeData()->hasFormat(CHART_MIME_TYPE)) {
    event->acceptProposedAction();
  }
}

void ChartView::dragMoveEvent(QDragMoveEvent* event) {
  if (event->mimeData()->hasFormat(CHART_MIME_TYPE)) {
    event->accept();
  }
}

void ChartView::resetChartCache() {
  chart_pixmap = QPixmap();
  viewport()->update();
}

void ChartView::startAnimation() {
  auto* eff = new QGraphicsOpacityEffect(this);
  viewport()->setGraphicsEffect(eff);

  // Setup a fast, linear fade
  auto* a = new QPropertyAnimation(eff, "opacity");
  a->setDuration(150);
  a->setStartValue(0.0);
  a->setEndValue(1.0);
  a->setEasingCurve(QEasingCurve::OutQuad);

  connect(a, &QPropertyAnimation::finished, [this]() {
    viewport()->setGraphicsEffect(nullptr);
  });

  a->start(QPropertyAnimation::DeleteWhenStopped);
}

void ChartView::paintEvent(QPaintEvent* event) {
  // If live streaming, bypass the pixmap cache to ensure smooth real-time updates
  if (StreamManager::stream()->liveStreaming()) {
    QChartView::paintEvent(event);
    return;
  }

  // Cache the background and static elements of the chart
  if (chart_pixmap.isNull() || chart_pixmap.size() != viewport()->size() * devicePixelRatio()) {
    updateCache();
  }

  QPainter painter(viewport());
  painter.setRenderHints(QPainter::Antialiasing);
  painter.drawPixmap(0, 0, chart_pixmap);

  // Draw dynamic overlays (Timeline, Tooltips, Rubberband)
  QRectF exposed_rect = mapToScene(event->region().boundingRect()).boundingRect();
  drawForeground(&painter, exposed_rect);
}

void ChartView::updateCache() {
  const qreal dpr = devicePixelRatio();
  chart_pixmap = QPixmap(viewport()->size() * dpr);
  chart_pixmap.setDevicePixelRatio(dpr);
  chart_pixmap.fill(palette().color(QPalette::Base));

  QPainter p(&chart_pixmap);
  p.setRenderHint(QPainter::Antialiasing);
  scene()->render(&p, QRectF(0, 0, viewport()->width(), viewport()->height()), viewport()->rect());
}

void ChartView::drawBackground(QPainter *painter, const QRectF &rect) {
  painter->fillRect(rect, palette().color(QPalette::Base));
}

void ChartView::drawForeground(QPainter *painter, const QRectF &rect) {
  drawTimeline(painter);
  drawSignalValue(painter);
  // draw track points
  painter->setPen(Qt::NoPen);
  qreal track_line_x = -1;
  for (auto &s : chart_->sigs_) {
    if (!s.track_pt.isNull() && s.series->isVisible()) {
      painter->setBrush(s.series->color().darker(125));
      QPointF pos = chart_->mapToPosition(s.track_pt);
      painter->drawEllipse(pos, 5.5, 5.5);
      track_line_x = std::max(track_line_x, pos.x());
    }
  }

  if (track_line_x > 0) {
    auto plot_area = chart_->plotArea();
    painter->setPen(QPen(Qt::darkGray, 1, Qt::DashLine));
    painter->drawLine(QPointF{track_line_x, plot_area.top()}, QPointF{track_line_x, plot_area.bottom()});
  }

  // paint points. OpenGL mode lacks certain features (such as showing points)
  painter->setPen(Qt::NoPen);
  for (auto &s : chart_->sigs_) {
    if (s.series->useOpenGL() && s.series->isVisible() && s.series->pointsVisible()) {
      auto first = std::ranges::lower_bound(s.vals, chart_->axis_x_->min(), {}, &QPointF::x);
      auto last = std::ranges::lower_bound(first, s.vals.end(), chart_->axis_x_->max(), {}, &QPointF::x);
      painter->setBrush(s.series->color());
      for (auto it = first; it != last; ++it) {
        painter->drawEllipse(chart_->mapToPosition(*it), 4, 4);
      }
    }
  }

  drawRubberBandTimeRange(painter);
}

void ChartView::drawRubberBandTimeRange(QPainter *painter) {
  auto rubber = findChild<QRubberBand *>();
  if (rubber && rubber->isVisible() && rubber->width() > 1) {
    painter->setPen(Qt::white);
    auto rubber_rect = rubber->geometry().normalized();
    for (const auto &pt : {rubber_rect.bottomLeft(), rubber_rect.bottomRight()}) {
      QString sec = QString::number(secondsAtPoint(pt), 'f', 2);
      auto r = painter->fontMetrics().boundingRect(sec).adjusted(-6, -AXIS_X_TOP_MARGIN, 6, AXIS_X_TOP_MARGIN);
      pt == rubber_rect.bottomLeft() ? r.moveTopRight(pt + QPoint{0, 2}) : r.moveTopLeft(pt + QPoint{0, 2});
      painter->fillRect(r, Qt::gray);
      painter->drawText(r, Qt::AlignCenter, sec);
    }
  }
}

void ChartView::drawTimeline(QPainter *painter) {
  const auto plot_area = chart_->plotArea();
  qreal x = chart_->mapToPosition({cur_sec, 0}).x();
  // Snap to the physical pixel grid
  qreal dpr = devicePixelRatioF();
  x = std::round(x * dpr) / dpr;
  x = std::clamp(x, plot_area.left(), plot_area.right());
  painter->setPen(QPen(chart_->titleBrush().color(), 1));
  painter->drawLine(QPointF{x, plot_area.top() - 1}, QPointF{x, plot_area.bottom() + 1});

  // draw current time under the axis-x
  QString time_str = QString::number(cur_sec, 'f', 2);
  QSize time_str_size = QFontMetrics(chart_->axis_x_->labelsFont()).size(Qt::TextSingleLine, time_str) + QSize(8, 2);
  QRectF time_str_rect(QPointF(x - time_str_size.width() / 2.0, plot_area.bottom() + AXIS_X_TOP_MARGIN), time_str_size);
  QPainterPath path;
  path.addRoundedRect(time_str_rect, 3, 3);
  painter->fillPath(path, utils::isDarkTheme() ? Qt::darkGray : Qt::gray);
  painter->setPen(palette().color(QPalette::BrightText));
  painter->setFont(chart_->axis_x_->labelsFont());
  painter->drawText(time_str_rect, Qt::AlignCenter, time_str);
}

void ChartView::drawSignalValue(QPainter *painter) {
  auto item_group = qgraphicsitem_cast<QGraphicsItemGroup *>(chart_->legend()->childItems()[0]);
  assert(item_group != nullptr);
  auto legend_markers = item_group->childItems();
  assert(legend_markers.size() == chart_->sigs_.size());

  painter->setFont(signal_value_font);
  painter->setPen(chart_->legend()->labelColor());
  int i = 0;
  for (auto &s : chart_->sigs_) {
    auto it = std::lower_bound(s.vals.crbegin(), s.vals.crend(), cur_sec,
                               [](auto &p, double x) { return p.x() > x + EPSILON; });
    QString value = (it != s.vals.crend() && it->x() >= chart_->axis_x_->min()) ? s.sig->formatValue(it->y()) : "--";
    QRectF marker_rect = legend_markers[i++]->sceneBoundingRect();
    QRectF value_rect(marker_rect.bottomLeft() - QPoint(0, 1), marker_rect.size());
    QString elided_val = painter->fontMetrics().elidedText(value, Qt::ElideRight, value_rect.width());
    painter->drawText(value_rect, Qt::AlignHCenter | Qt::AlignTop, elided_val);
  }
}
