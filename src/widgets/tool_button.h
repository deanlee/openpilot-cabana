#pragma once

#include <QPainter>
#include <QToolButton>
#include <optional>


class ToolButton : public QToolButton {
  Q_OBJECT
 public:
  ToolButton(const QString& icon = {}, const QString& tooltip = {}, QWidget* parent = nullptr);
  void setHoverColor(const QColor& color) { hover_color = color; }
  void setIcon(const QString &icon);

 private:
  void enterEvent(QEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void onSettingsChanged();
  void refreshIcon(std::optional<QColor> tint_color = std::nullopt);
  void changeEvent(QEvent* event) override;

  QColor hover_color;
  QString icon_str;
  int theme;
};
