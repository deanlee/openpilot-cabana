#pragma once

#include <QAbstractItemModel>
#include <memory>

#include "chart/sparkline.h"

class SignalTreeModel : public QAbstractItemModel {
  Q_OBJECT
public:
  struct Item {
    enum Type {Root, Sig, Name, Size, Node, Endian, Signed, Offset, Factor, SignalType, MultiplexValue, ExtraInfo, Unit, Comment, Min, Max, Desc };
    ~Item() { qDeleteAll(children); }
    inline int row() { return parent->children.indexOf(this); }

    Type type = Type::Root;
    Item *parent = nullptr;
    QList<Item *> children;

    const cabana::Signal *sig = nullptr;
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
  bool saveSignal(const cabana::Signal *origin_s, cabana::Signal &s);
  Item *getItem(const QModelIndex &index) const;
  int signalRow(const cabana::Signal *sig) const;

private:
  void insertItem(SignalTreeModel::Item *root_item, int pos, const cabana::Signal *sig);
  void handleSignalAdded(MessageId id, const cabana::Signal *sig);
  void handleSignalUpdated(const cabana::Signal *sig);
  void handleSignalRemoved(const cabana::Signal *sig);
  void handleMsgChanged(MessageId id);
  void refresh();

  MessageId msg_id;
  QString filter_str;
  std::unique_ptr<Item> root;
  friend class SignalView;
  friend class SignalItemDelegate;
};

QString signalTypeToString(cabana::Signal::Type type);
