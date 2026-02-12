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
  enum Column { NAME = 0, SOURCE, ADDRESS, NODE, FREQ, COUNT, DATA, MAX_COLUMN };

  struct Item {
    MessageId id;
    QString name;
    QString node;
    const MessageSnapshot* data = nullptr;
    QString address_hex;
    mutable float last_freq = -1.0f;
    mutable QString freq_str;
  };

  MessageModel(QObject* parent);
  inline bool isInactiveMessagesVisible() const { return show_inactive_; }
  const Item* getItem(const QModelIndex& index) const {
    if (!index.isValid() || index.row() >= static_cast<int>(items_.size())) return nullptr;
    return &items_[index.row()];
  }
  inline int getDbcMessageCount() const { return dbc_msg_count_; }
  inline int getSignalCount() const { return signal_count_; }
  inline int getRowForMessageId(const MessageId& id) const {
    auto it = std::ranges::find(items_, id, &Item::id);
    return (it != items_.end()) ? std::distance(items_.begin(), it) : -1;
  }
  void setFilterStrings(const QMap<int, QString>& filters);
  void setInactiveMessagesVisible(bool show);
  void onSnapshotsUpdated(const std::set<MessageId>* ids, bool needs_rebuild);
  void rebuild();

  // QAbstractTableModel overrides
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override { return Column::MAX_COLUMN; }
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;
  int rowCount(const QModelIndex& parent = QModelIndex()) const override { return items_.size(); }
  void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

 private:
  struct FilterRange {
    double min = -std::numeric_limits<double>::infinity();
    double max = std::numeric_limits<double>::infinity();
    bool is_exact = false;
  };

  std::optional<FilterRange> parseFilter(QString filter, int base = 10);
  std::vector<Item> fetchItems() const;
  void sortItems(std::vector<MessageModel::Item>& items) const;
  bool match(const MessageModel::Item& id) const;
  QString formatFreq(const Item& item) const;

  std::vector<Item> items_;
  QMap<int, QString> filters_;
  QMap<int, FilterRange> filter_ranges_;
  bool show_inactive_ = true;
  int sort_column = 0;
  Qt::SortOrder sort_order = Qt::AscendingOrder;
  int sort_threshold_ = 0;
  int dbc_msg_count_ = 0;
  int signal_count_ = 0;
};
