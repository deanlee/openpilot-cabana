#include "common.h"

#include <QApplication>
#include <QStyle>

#include <cmath>

#include "utils/util.h"
#include "modules/settings/settings.h"

// ToolButton

ToolButton::ToolButton(const QString& icon, const QString& tooltip, QWidget* parent) 
    : QToolButton(parent), icon_str(icon) {
  setToolTip(tooltip);
  setAutoRaise(true);
  setFocusPolicy(Qt::NoFocus);

  const int metric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
  setIconSize({metric, metric});

  refreshIcon();
  connect(&settings, &Settings::changed, this, &ToolButton::onSettingsChanged);
}

void ToolButton::setIcon(const QString& icon) {
  icon_str = icon;
  refreshIcon();
}

void ToolButton::refreshIcon(std::optional<QColor> tint_color) {
  if (icon_str.isEmpty()) return;

  QIcon new_icon = utils::icon(icon_str, iconSize(), tint_color);
  QToolButton::setIcon(new_icon);
}

void ToolButton::onSettingsChanged() {
  // Only refresh if the actual theme property has changed
  if (std::exchange(theme, settings.theme) != settings.theme) {
    refreshIcon();
  }
}

void ToolButton::enterEvent(QEvent* event) {
  QToolButton::enterEvent(event);
  if (hover_color.isValid()) {
    refreshIcon(hover_color);
  }
}

void ToolButton::leaveEvent(QEvent* event) {
  QToolButton::leaveEvent(event);
  refreshIcon(); // Reverts to default theme color
}

void ToolButton::changeEvent(QEvent* event) {
  if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
    refreshIcon();
  }
  QToolButton::changeEvent(event);
}

// TabBar

int TabBar::addTab(const QString &text) {
  int index = QTabBar::addTab(text);
  QToolButton *btn = new ToolButton("x", tr("Close Tab"));
  int width = style()->pixelMetric(QStyle::PM_TabCloseIndicatorWidth, nullptr, btn);
  int height = style()->pixelMetric(QStyle::PM_TabCloseIndicatorHeight, nullptr, btn);
  btn->setFixedSize({width, height});
  setTabButton(index, QTabBar::RightSide, btn);
  connect(btn, &QToolButton::clicked, this, &TabBar::closeTabClicked);
  return index;
}

void TabBar::closeTabClicked() {
  QObject *object = sender();
  for (int i = 0; i < count(); ++i) {
    if (tabButton(i, QTabBar::RightSide) == object) {
      emit tabCloseRequested(i);
      break;
    }
  }
}

// LogSlider

void LogSlider::setRange(double min, double max) {
  log_min = factor * std::log10(min);
  log_max = factor * std::log10(max);
  QSlider::setRange(min, max);
  setValue(QSlider::value());
}

int LogSlider::value() const {
  double v = log_min + (log_max - log_min) * ((QSlider::value() - minimum()) / double(maximum() - minimum()));
  return std::lround(std::pow(10, v / factor));
}

void LogSlider::setValue(int v) {
  double log_v = std::clamp(factor * std::log10(v), log_min, log_max);
  v = minimum() + (maximum() - minimum()) * ((log_v - log_min) / (log_max - log_min));
  QSlider::setValue(v);
}

// ElidedLabel

ElidedLabel::ElidedLabel(QWidget *parent) : QLabel(parent) {
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  setMinimumWidth(1);
}

void ElidedLabel::paintEvent(QPaintEvent* event) {
  QPainter painter(this);
  QString elidedText = fontMetrics().elidedText(text(), Qt::ElideRight, width());
  painter.drawText(rect(), alignment(), elidedText);
}

QFrame* createVLine(QWidget* parent) {
  QFrame* v_line = new QFrame();
  v_line->setFixedHeight(16);
  v_line->setFrameShape(QFrame::VLine);
  v_line->setFrameShadow(QFrame::Plain);
  v_line->setStyleSheet("color: palette(mid);");
  return v_line;
}
