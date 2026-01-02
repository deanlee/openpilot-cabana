#pragma once

#include <QAbstractTableModel>
#include <QMap>
#include <QVariant>
#include <vector>

#include "dbc/dbcmanager.h"
#include "streams/abstractstream.h"

class MessageTableModel : public QAbstractTableModel {
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

  MessageTableModel(QObject *parent) : QAbstractTableModel(parent) {}
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override { return Column::DATA + 1; }
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
  int rowCount(const QModelIndex &parent = QModelIndex()) const override { return items_.size(); }
  void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
  void setFilterStrings(const QMap<int, QString> &filters);
  void showInactivemessages(bool show);
  void onSnapshotsUpdated(const std::set<MessageId> *ids, bool needs_rebuild);
  bool filterAndSort();
  void dbcModified();

  struct Item {
    MessageId id;
    QString name;
    QString node;
    const CanData* data = nullptr;

    bool operator==(const Item &other) const {
      return id == other.id && name == other.name && node == other.node;
    }
  };
  std::vector<Item> items_;
  bool show_inactive_messages = true;

private:
  void sortItems(std::vector<MessageTableModel::Item> &items);
  bool match(const MessageTableModel::Item &id);

  QMap<int, QString> filters_;
  std::set<MessageId> dbc_messages_;
  int sort_column = 0;
  Qt::SortOrder sort_order = Qt::AscendingOrder;
  int sort_threshold_ = 0;
};
