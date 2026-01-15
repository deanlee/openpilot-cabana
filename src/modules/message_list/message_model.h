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
  void showInactivemessages(bool show);
  void onSnapshotsUpdated(const std::set<MessageId> *ids, bool needs_rebuild);
  inline void resetState() { filterAndSort(); }
  bool filterAndSort();

  struct Item {
    MessageId id;
    QString name;
    QString node;
    const MessageState* data = nullptr;
    QString address_hex;

    bool operator==(const Item &other) const {
      return id == other.id && name == other.name && node == other.node;
    }
  };
  std::vector<Item> items_;
  bool show_inactive_messages = true;

signals:
  void uiUpdateRequired();

private:
  void sortItems(std::vector<MessageModel::Item> &items);
  bool match(const MessageModel::Item &id);

  QMap<int, QString> filters_;
  int sort_column = 0;
  Qt::SortOrder sort_order = Qt::AscendingOrder;
  int sort_threshold_ = 0;
  QColor disabled_color_;
};
