#pragma once

#include <QAbstractTableModel>
#include <QColor>
#include <array>
#include <optional>
#include <vector>

#include "core/dbc/dbc_manager.h"

const int VERTICAL_HEADER_WIDTH = 30;

class BinaryModel : public QAbstractTableModel {
 public:
  BinaryModel(QObject* parent) : QAbstractTableModel(parent) {}
  void refresh();
  void updateBorders();
  void updateState();
  void updateItem(int row, int col, uint8_t val, const QColor& color);
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  int rowCount(const QModelIndex& parent = QModelIndex()) const override { return row_count; }
  int columnCount(const QModelIndex& parent = QModelIndex()) const override { return column_count; }
  QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override {
    return createIndex(row, column, (void*)&items[row * column_count + column]);
  }
  Qt::ItemFlags flags(const QModelIndex& index) const override {
    return (index.column() == column_count - 1) ? Qt::ItemIsEnabled : Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  }
  const std::vector<std::array<uint32_t, 8>>& getBitFlipChanges(size_t msg_size);

  struct BitFlipTracker {
    std::optional<std::pair<double, double>> time_range;
    std::vector<std::array<uint32_t, 8>> flip_counts;
  } bit_flip_tracker;

  struct Item {
    QColor bg_color = QColor(102, 86, 169, 255);
    bool is_msb = false;
    bool is_lsb = false;
    uint8_t val;
    QList<const dbc::Signal*> sigs;
    bool valid = false;
    float intensity = 0.0f;
    uint32_t last_flips = 0;

    struct Borders {
      uint8_t left : 1, right : 1, top : 1, bottom : 1;
      uint8_t top_left : 1, top_right : 1, bottom_left : 1, bottom_right : 1;
    } borders;
  };
  std::vector<Item> items;
  bool heatmap_live_mode = true;
  MessageId msg_id;
  int row_count = 0;
  const int column_count = 9;

  QColor calculateBitHeatColor(Item &item, uint32_t flips, uint32_t max_flips, bool is_light);
};

QString signalToolTip(const dbc::Signal *sig);
