#pragma once
#include <QAbstractTableModel>
#include <QColor>
#include <deque>
#include <vector>

#include "core/dbc/dbc_manager.h"
#include "core/streams/message_state.h"

class MessageHistoryModel : public QAbstractTableModel {
  Q_OBJECT

public:
  struct SignalColumn {
    QString display_name;
    dbc::Signal *sig;
  };

  struct LogEntry {
    uint64_t mono_ns = 0;
    std::vector<double> sig_values;
    uint8_t size = 0;
    std::array<uint8_t, MAX_CAN_LEN> data;
    std::array<uint32_t, MAX_CAN_LEN> colors;
  };

  MessageHistoryModel(QObject *parent) : QAbstractTableModel(parent) {}
  void setMessage(const MessageId &message_id);
  void updateState(bool clear = false);
  void setFilter(int sig_idx, const QString &value, std::function<bool(double, double)> cmp);
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
  QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override {
    if (!hasIndex(row, column, parent)) return {};
    return createIndex(row, column, (void*)(&messages[row]));
  }
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  void fetchMore(const QModelIndex &parent) override;
  bool canFetchMore(const QModelIndex &parent) const override;
  int rowCount(const QModelIndex &parent = QModelIndex()) const override { return messages.size(); }
  int columnCount(const QModelIndex &parent = QModelIndex()) const override { return !isHexMode() ? sigs.size() + 1 : 2; }
  inline bool isHexMode() const { return sigs.empty() || hex_mode; }
  void reset();
  void setHexMode(bool hex_mode);
  void setPaused() { setPauseState(true); }
  void setResumed() { setPauseState(false); }
  const std::vector<SignalColumn> &messageSignals() const { return sigs; }
  void setPauseState(bool paused);
  void fetchData(int insert_pos_idx, uint64_t from_time, uint64_t min_time);

  MessageId msg_id;

private:
  MessageState hex_colors;
  const int batch_size = 50;
  int filter_sig_idx = -1;
  double filter_value = 0;
  std::function<bool(double, double)> filter_cmp = nullptr;
  std::deque<LogEntry> messages;
  std::vector<SignalColumn> sigs;
  bool hex_mode = false;
  bool is_paused = false;
};
