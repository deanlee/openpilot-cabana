#include "time_label.h"

#include <QFontDatabase>
#include <QPainter>

#include "modules/settings/settings.h"
#include "modules/system/stream_manager.h"
#include "utils/util.h"

TimeLabel::TimeLabel(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  fixed_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  bold_font = fixed_font;
  bold_font.setBold(true);
}

void TimeLabel::setTime(double cur, double total) {
  if (cur != current_sec || total != total_sec) {
    current_sec = cur;
    total_sec = total;
    updateTime();
  }
}

void TimeLabel::paintEvent(QPaintEvent* event) {
  QPainter p(this);
  p.fillRect(rect(), palette().window());

  p.setPen(palette().text().color());

  // 1. Draw Bold Current Time
  p.setFont(bold_font);
  p.drawText(0, 0, cur_time_width, height(), Qt::AlignLeft | Qt::AlignVCenter, current_sec_text);

  // 2. Draw Regular Total Time
  if (!total_sec_text.isEmpty()) {
    p.setFont(fixed_font);
    p.drawText(cur_time_width, 0, width() - cur_time_width, height(), Qt::AlignLeft | Qt::AlignVCenter, total_sec_text);
  }
}

void TimeLabel::updateTime() {
  current_sec_text = formatTime(current_sec, true);
  cur_time_width = QFontMetrics(bold_font).horizontalAdvance(current_sec_text);

  if (total_sec >= 0) {
    total_sec_text = " / " + formatTime(total_sec);
  } else {
    total_sec_text.clear();
  }
  update();
}

QString TimeLabel::formatTime(double sec, bool include_milliseconds) {
  const bool abs = settings.absolute_time;
  if (abs) {
    sec = StreamManager::stream()->beginDateTime().addMSecs(sec * 1000).toMSecsSinceEpoch() / 1000.0;
  }
  return utils::formatSeconds(sec, include_milliseconds, abs);
}

void TimeLabel::mousePressEvent(QMouseEvent* event) {
  settings.absolute_time = !settings.absolute_time;
  updateTime();
  setToolTip(settings.absolute_time ? tr("Elapsed time") : tr("Absolute time"));
  QWidget::mousePressEvent(event);
}
