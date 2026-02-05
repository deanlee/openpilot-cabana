#pragma once

#include <QAbstractTableModel>
#include <QMap>
#include <QVariant>
#include <vector>

#include "core/dbc/dbc_manager.h"
#include "core/streams/abstract_stream.h"

class MessageModel : public QAbstractTableModel {
Q_OBJECT

public:
  enum Column {
    NAME = 0,
    SOURCE,
    ADDRESS,
    NODE,
    FREQ,
    COUNT,
    DATA,
  };

  MessageModel(QObject *parent);
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override { return Column::DATA + 1; }
  QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override {
    if (!hasIndex(row, column, parent)) return {};
    return createIndex(row, column, (void*)(&items_[row]));
  }
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
  int rowCount(const QModelIndex &parent = QModelIndex()) const override { return items_.size(); }
  void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
  void setFilterStrings(const QMap<int, QString> &filters);
  void setInactiveMessagesVisible(bool show);
  void onSnapshotsUpdated(const std::set<MessageId> *ids, bool needs_rebuild);
  void rebuild();

  struct Item {
    MessageId id;
    QString name;
    QString node;
    const MessageSnapshot* data = nullptr;
    QString address_hex;
    mutable float last_freq = -1.0f;
    mutable QString freq_str;
  };
  std::vector<Item> items_;
  bool show_inactive_ = true;

private:
 struct FilterRange {
   double min = -std::numeric_limits<double>::infinity();
   double max = std::numeric_limits<double>::infinity();
   bool is_exact = false;
 };

  std::optional<FilterRange> parseFilter(QString filter, int base = 10);
  std::vector<Item> fetchItems() const;
  void sortItems(std::vector<MessageModel::Item> &items) const;
  bool match(const MessageModel::Item &id) const;
  QString formatFreq(const Item &item) const;

  QMap<int, QString> filters_;
  QMap<int, FilterRange> filter_ranges_;
  int sort_column = 0;
  Qt::SortOrder sort_order = Qt::AscendingOrder;
  int sort_threshold_ = 0;
  QColor disabled_color_;
};
