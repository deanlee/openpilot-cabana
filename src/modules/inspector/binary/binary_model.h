#pragma once

#include <QAbstractTableModel>
#include <QColor>
#include <QFont>
#include <QSet>
#include <array>
#include <optional>
#include <vector>

#include "core/dbc/dbc_manager.h"
#include "core/streams/message_state.h"

// 32-32px is the "sweet spot" for technical touch interfaces
constexpr int CELL_WIDTH = 32;
constexpr int CELL_HEIGHT = 32;

class BinaryModel : public QAbstractTableModel {
 public:
  inline static constexpr int INVALID_BIT = 0xFF;

  struct Item {
    QColor bg_color = QColor(102, 86, 169, 255);
    bool is_msb = false;
    bool is_lsb = false;
    uint8_t val = INVALID_BIT;
    QList<const dbc::Signal*> sigs;
    float intensity = 0.0f;
    uint32_t last_flips = 0;

    struct Borders {
      uint8_t left : 1, right : 1, top : 1, bottom : 1;
      uint8_t top_left : 1, top_right : 1, bottom_left : 1, bottom_right : 1;
    } borders;
  };

  explicit BinaryModel(QObject* parent);
  const Item *getItem(const QModelIndex& index) const;
  void setMessage(const MessageId& message_id);
  void initializeItems();
  void mapSignalsToItems(const dbc::Msg* msg);
  void setHeatmapMode(bool live);
  void rebuild();
  void updateBorders();
  void updateState();
  void updateSignalCells(const dbc::Signal* sig);
  QSet<const dbc::Signal*> getOverlappingSignals() const;
  const std::array<std::array<uint32_t, 8>, MAX_CAN_LEN>& getBitFlipChanges(size_t msg_size);

  // QAbstractTableModel overrides
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  int rowCount(const QModelIndex& parent = QModelIndex()) const override { return row_count; }
  int columnCount(const QModelIndex& parent = QModelIndex()) const override { return column_count; }
  Qt::ItemFlags flags(const QModelIndex& index) const override {
    return (index.column() == column_count - 1) ? Qt::ItemIsEnabled : Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  }

  MessageId msg_id;

 private:
  struct BitFlipTracker {
    std::optional<std::pair<double, double>> time_range;
    std::array<std::array<uint32_t, 8>, MAX_CAN_LEN> flip_counts;
  } bit_flip_tracker;

  int row_count = 0;
  const int column_count = 9;
  bool heatmap_live_mode = true;
  std::vector<Item> items;
  QFont header_font_;

  bool syncRowItems(int row, const MessageSnapshot* msg, const std::array<uint32_t, 8>& row_flips, float log_max,
                    bool is_light, const QColor& base_bg, float decay);
  QColor calculateBitHeatColor(Item& item, uint32_t flips, float log_max, bool is_light, const QColor& base_bg,
                               float decay_factor);
  bool updateItem(int row, int col, uint8_t val, const QColor& color);
};

QString signalToolTip(const dbc::Signal* sig);
