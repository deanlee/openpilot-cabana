#include <QHeaderView>
#include <QMap>
#include <QPointer>

#include "widgets/debounced_line_edit.h"

class MessageHeader : public QHeaderView {
  Q_OBJECT

 public:
  explicit MessageHeader(QWidget* parent = nullptr);

  void setModel(QAbstractItemModel* model) override;
  QSize sizeHint() const override;

 public slots:
  void updateFilters();
  void updateHeaderPositions();

 protected:
  void updateGeometries() override;

 private:
  QString getFilterTooltip(int col) const;

  QMap<int, QPointer<DebouncedLineEdit>> editors;
  QTimer filter_timer;
  int cached_editor_height = 0;
  bool is_updating = false;
};
