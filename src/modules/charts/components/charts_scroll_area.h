#pragma once

#include <QScrollArea>
#include <QTimer>

class ChartsScrollArea : public QScrollArea {
  Q_OBJECT

 public:
  explicit ChartsScrollArea(QWidget* parent = nullptr);
  void startAutoScroll();
  void stopAutoScroll();

 private:
  static constexpr int kAutoScrollIntervalMs = 16;  // ~60Hz for smooth scrolling
  static constexpr int kAutoScrollMargin = 60;       // Edge sensitivity zone in pixels
  static constexpr int kMinScrollSpeed = 2;
  static constexpr int kMaxScrollSpeed = 30;

  int autoScrollDelta(int pos, int viewportHeight) const;
  void doAutoScroll();

  QTimer* auto_scroll_timer_ = nullptr;
};
