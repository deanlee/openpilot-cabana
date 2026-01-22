#pragma once

#include <QLabel>
#include <QPainter>
#include <QSlider>
#include <QTabBar>
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

class TabBar : public QTabBar {
  Q_OBJECT

 public:
  TabBar(QWidget* parent) : QTabBar(parent) {}
  int addTab(const QString& text);

 private:
  void closeTabClicked();
};

class LogSlider : public QSlider {
  Q_OBJECT

public:
  LogSlider(double factor, Qt::Orientation orientation, QWidget *parent = nullptr) : factor(factor), QSlider(orientation, parent) {}
  void setRange(double min, double max);
  int value() const;
  void setValue(int v);

private:
  double factor, log_min = 0, log_max = 1;
};

class ElidedLabel : public QLabel {
public:
  ElidedLabel(QWidget *parent);
  void paintEvent(QPaintEvent *event) override;
};

QFrame *createVLine(QWidget *parent);
