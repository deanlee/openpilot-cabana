#include <QHeaderView>
#include <QLineEdit>
#include <QMap>
#include <QPointer>
#include <QTimer>

class MessageHeader : public QHeaderView {
  Q_OBJECT

 public:
  explicit MessageHeader(QWidget* parent = nullptr);
  ~MessageHeader();

  void setModel(QAbstractItemModel* model) override;
  QSize sizeHint() const override;

 public slots:
  void updateFilters();
  void updateHeaderPositions();
  void clearEditors(); 

 protected:
  void updateGeometries() override;

 private:
  QMap<int, QPointer<QLineEdit>> editors;
  QTimer filter_timer;
  int cached_editor_height = 0;
  bool is_updating = false;
};
