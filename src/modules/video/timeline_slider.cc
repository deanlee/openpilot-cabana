#include "timeline_slider.h"

#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionSlider>

#include "playback_view.h"
#include "replay/include/timeline.h"
#include "core/streams/replaystream.h"
#include "modules/system/stream_manager.h"

static Replay* getReplay() {
  auto stream = qobject_cast<ReplayStream*>(StreamManager::stream());
  return stream ? stream->getReplay() : nullptr;
}

Slider::Slider(QWidget* parent) : QSlider(Qt::Horizontal, parent) {
  setMouseTracking(true);
}

void Slider::paintEvent(QPaintEvent* ev) {
  QPainter p(this);

  QStyleOptionSlider opt;
  initStyleOption(&opt);
  QRect handle_rect = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
  QRect groove_rect = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);

  // Adjust groove height to match handle height
  int handle_height = handle_rect.height();
  groove_rect.setHeight(handle_height * 0.5);
  groove_rect.moveCenter(QPoint(groove_rect.center().x(), rect().center().y()));

  p.fillRect(groove_rect, timeline_colors[(int)TimelineType::None]);

  double min = minimum() / factor;
  double max = maximum() / factor;

  auto fillRange = [&](double begin, double end, const QColor& color) {
    if (begin > max || end < min) return;

    QRect r = groove_rect;
    r.setLeft(((std::max(min, begin) - min) / (max - min)) * width());
    r.setRight(((std::min(max, end) - min) / (max - min)) * width());
    p.fillRect(r, color);
  };

  if (auto replay = getReplay()) {
    for (const auto& entry : *replay->getTimeline()) {
      fillRange(entry.start_time, entry.end_time, timeline_colors[(int)entry.type]);
    }

    QColor empty_color = palette().color(QPalette::Window);
    empty_color.setAlpha(160);
    const auto event_data = replay->getEventData();
    for (const auto& [n, _] : replay->route().segments()) {
      if (!event_data->isSegmentLoaded(n))
        fillRange(n * 60.0, (n + 1) * 60.0, empty_color);
    }
  }

  opt.minimum = minimum();
  opt.maximum = maximum();
  opt.subControls = QStyle::SC_SliderHandle;
  opt.sliderPosition = value();
  style()->drawComplexControl(QStyle::CC_Slider, &opt, &p);

  if (thumbnail_dispaly_time >= 0) {
    int left = (thumbnail_dispaly_time - min) * width() / (max - min) - 1;
    QRect rc(left, rect().top() + 1, 2, rect().height() - 2);
    p.setBrush(palette().highlight());
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rc, 1.5, 1.5);
  }
}

void Slider::mousePressEvent(QMouseEvent* e) {
  QSlider::mousePressEvent(e);
  if (e->button() == Qt::LeftButton && !isSliderDown()) {
    setValue(minimum() + ((maximum() - minimum()) * e->x()) / width());
    emit sliderReleased();
  }
}
