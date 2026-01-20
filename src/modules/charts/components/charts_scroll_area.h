#pragma once

#include <QScrollArea>
#include <QTimer>

class ChartsContainer;

class ChartsScrollArea : public QScrollArea {
  Q_OBJECT

 public:
  explicit ChartsScrollArea(QWidget* parent = nullptr);
  void startAutoScroll();
  void stopAutoScroll();
  void resizeEvent(QResizeEvent* event) override;

 private slots:
  void doAutoScroll();

private:
  ChartsContainer *container_;
  QTimer *auto_scroll_timer_;

  friend class ChartsPanel;
};
