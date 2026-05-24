#include "time_label.h"

#include <cmath>

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
  setToolTip(settings.absolute_time ? tr("Absolute time") : tr("Elapsed time"));
}

void TimeLabel::setTime(double cur, double total) {
  // Avoid strict float equality checks and skip unnecessary repaints.
  if (std::abs(cur - current_sec) > 1e-6 || std::abs(total - total_sec) > 1e-6) {
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
  const QString new_current_text = formatTime(current_sec, true);
  const int new_cur_width = QFontMetrics(bold_font).horizontalAdvance(new_current_text);
  QString new_total_text;

  if (total_sec >= 0) {
    new_total_text = " / " + formatTime(total_sec);
  }

  if (new_current_text == current_sec_text && new_total_text == total_sec_text && new_cur_width == cur_time_width) {
    return;
  }

  current_sec_text = new_current_text;
  total_sec_text = new_total_text;
  cur_time_width = new_cur_width;
  update();
}

QString TimeLabel::formatTime(double sec, bool include_milliseconds) {
  const bool abs = settings.absolute_time;
  if (abs) {
    auto* stream = StreamManager::stream();
    if (stream) {
      sec = stream->beginDateTime().addMSecs(sec * 1000).toMSecsSinceEpoch() / 1000.0;
    }
  }
  return utils::formatSeconds(sec, include_milliseconds, abs);
}

void TimeLabel::mousePressEvent(QMouseEvent* event) {
  settings.absolute_time = !settings.absolute_time;
  updateTime();
  setToolTip(settings.absolute_time ? tr("Absolute time") : tr("Elapsed time"));
  QWidget::mousePressEvent(event);
}
