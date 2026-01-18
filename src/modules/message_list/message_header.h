#include <QHeaderView>
#include <QLineEdit>
#include <QMap>
#include <QPointer>
#include <QTimer>


class MessageHeader : public QHeaderView {
  // https://stackoverflow.com/a/44346317
  Q_OBJECT
public:
  MessageHeader(QWidget *parent);
  void updateHeaderPositions();
  void updateGeometries() override;
  QSize sizeHint() const override;
  void updateFilters();

  QMap<int, QLineEdit *> editors;
};
