#pragma once

#include <QEvent>
#include <QPainter>
#include <QSplitter>

class PanelSplitter : public QSplitter {
 public:
  explicit PanelSplitter(Qt::Orientation orientation, QWidget* parent = nullptr);
  using QSplitter::QSplitter;

 protected:
  QSplitterHandle* createHandle() override { return new Handle(orientation(), this); }

  class Handle : public QSplitterHandle {
   public:
    Handle(Qt::Orientation o, QSplitter* p);

   protected:
    void paintEvent(QPaintEvent* e) override;
    bool event(QEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

    bool is_dragging = false;
  };
};
