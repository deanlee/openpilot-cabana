#pragma once
#include <QAbstractTableModel>
#include <QColor>
#include <deque>
#include <vector>

#include "dbc/dbcmanager.h"
#include "streams/candata.h"

class MessageLogModel : public QAbstractTableModel {
  Q_OBJECT

public:
  MessageLogModel(QObject *parent) : QAbstractTableModel(parent) {}
  void setMessage(const MessageId &message_id);
  void updateState(bool clear = false);
  void setFilter(int sig_idx, const QString &value, std::function<bool(double, double)> cmp);
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  void fetchMore(const QModelIndex &parent) override;
  bool canFetchMore(const QModelIndex &parent) const override;
  int rowCount(const QModelIndex &parent = QModelIndex()) const override { return messages.size(); }
  int columnCount(const QModelIndex &parent = QModelIndex()) const override { return !isHexMode() ? sigs.size() + 1 : 2; }
  inline bool isHexMode() const { return sigs.empty() || hex_mode; }
  void reset();
  void setHexMode(bool hex_mode);

  struct Message {
    uint64_t mono_time = 0;
    std::vector<double> sig_values;
    std::vector<uint8_t> data;
    std::vector<QColor> colors;
  };

  void fetchData(std::deque<Message>::iterator insert_pos, uint64_t from_time, uint64_t min_time);

  MessageId msg_id;
  CanData hex_colors;
  const int batch_size = 50;
  int filter_sig_idx = -1;
  double filter_value = 0;
  std::function<bool(double, double)> filter_cmp = nullptr;
  std::deque<Message> messages;
  std::vector<cabana::Signal *> sigs;
  bool hex_mode = false;
};
