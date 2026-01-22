#pragma once

#include <QAbstractItemModel>
#include <QSet>

#include <memory>

#include "modules/charts/sparkline.h"
#include "core/dbc/dbc_message.h"

enum SignalRole {
  IsChartedRole = Qt::UserRole + 10
};

class SignalTreeModel : public QAbstractItemModel {
  Q_OBJECT
public:
  struct Item {
    enum Type {Root, Sig, Name, Size, Node, Endian, Signed, Offset, Factor, SignalType, MultiplexValue, ExtraInfo, Unit, Comment, Min, Max, ValueTable };
    ~Item() { qDeleteAll(children); }
    inline int row() {
      if (parent) return parent->children.indexOf(this);
      return 0;
    }

    Type type = Type::Root;
    Item *parent = nullptr;
    QList<Item *> children;

    const dbc::Signal *sig = nullptr;
    QString title;
    bool highlight = false;
    QString sig_val = "-";
    Sparkline sparkline;
  };

  SignalTreeModel(QObject *parent);
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override { return 2; }
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex &index) const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;
  bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
  void setMessage(const MessageId &id);
  void setFilter(const QString &txt);
  bool saveSignal(const dbc::Signal *origin_s, dbc::Signal &s);
  Item *getItem(const QModelIndex &index) const;
  int signalRow(const dbc::Signal *sig) const;
  void updateChartedSignals(const QMap<MessageId, QSet<const dbc::Signal*>> &opened);
  void fetchMore(const QModelIndex &parent) override;
  bool canFetchMore(const QModelIndex &parent) const override;

private:
  bool hasChildren(const QModelIndex &parent) const override;
  void insertItem(SignalTreeModel::Item *root_item, int pos, const dbc::Signal *sig);
  void handleSignalAdded(MessageId id, const dbc::Signal *sig);
  void handleSignalUpdated(const dbc::Signal *sig);
  void handleSignalRemoved(const dbc::Signal *sig);
  void handleMsgChanged(MessageId id);
  void refresh();

  MessageId msg_id;
  QString filter_str;
  QMap<MessageId, QSet<const dbc::Signal*>> charted_signals_;
  std::unique_ptr<Item> root;
  friend class SignalEditor;
  friend class SignalTreeDelegate;
};

QString signalTypeToString(dbc::Signal::Type type);
