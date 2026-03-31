#pragma once

#include <QBasicTimer>
#include <QScrollArea>

class ChartsScrollArea : public QScrollArea {
  Q_OBJECT

 public:
  explicit ChartsScrollArea(QWidget* parent = nullptr);
  void startAutoScroll();
  void stopAutoScroll();

 protected:
  void timerEvent(QTimerEvent* event) override;

 private:
  static constexpr int kAutoScrollIntervalMs = 16;  // ~60Hz for smooth scrolling
  static constexpr int kAutoScrollMargin = 60;       // Edge sensitivity zone in pixels
  static constexpr int kMinScrollSpeed = 2;
  static constexpr int kMaxScrollSpeed = 30;

  int autoScrollDelta(int pos, int viewportHeight) const;

  QBasicTimer auto_scroll_timer_;
};
