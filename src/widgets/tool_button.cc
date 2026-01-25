#include "tool_button.h"

#include <QApplication>
#include <QStyle>

#include "modules/settings/settings.h"
#include "utils/util.h"

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
  refreshIcon();  // Reverts to default theme color
}

void ToolButton::changeEvent(QEvent* event) {
  if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
    refreshIcon();
  }
  QToolButton::changeEvent(event);
}
