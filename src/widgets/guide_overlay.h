#pragma

#include <QWidget>
#include <QPainter>

class MainWindow;

class GuideOverlay : public QWidget {
  Q_OBJECT
public:
  GuideOverlay(MainWindow *parent);

protected:
  void drawHelpForWidget(QPainter &painter, QWidget *w);
  void paintEvent(QPaintEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  bool eventFilter(QObject *obj, QEvent *event) override;
};
