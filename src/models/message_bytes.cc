#include "message_bytes.h"

#include <algorithm>
#include <QDebug>

#include "streams/abstractstream.h"

void MessageBytesModel::refresh() {
  beginResetModel();
  bit_flip_tracker = {};
  items.clear();
  if (auto dbc_msg = dbc()->msg(msg_id)) {
    row_count = dbc_msg->size;
    items.resize(row_count * column_count);
    for (auto sig : dbc_msg->getSignals()) {
      for (int j = 0; j < sig->size; ++j) {
        int pos = sig->is_little_endian ? flipBitPos(sig->start_bit + j) : flipBitPos(sig->start_bit) + j;
        int idx = column_count * (pos / 8) + pos % 8;
        if (idx >= items.size()) {
          qWarning() << "signal " << sig->name << "out of bounds.start_bit:" << sig->start_bit << "size:" << sig->size;
          break;
        }
        if (j == 0) sig->is_little_endian ? items[idx].is_lsb = true : items[idx].is_msb = true;
        if (j == sig->size - 1) sig->is_little_endian ? items[idx].is_msb = true : items[idx].is_lsb = true;

        auto &sigs = items[idx].sigs;
        sigs.push_back(sig);
        if (sigs.size() > 1) {
          std::sort(sigs.begin(), sigs.end(), [](auto l, auto r) { return l->size > r->size; });
        }
      }
    }
  } else {
    row_count = can->snapshot(msg_id)->dat.size();
    items.resize(row_count * column_count);
  }
  endResetModel();
  updateState();
}

void MessageBytesModel::updateItem(int row, int col, uint8_t val, const QColor &color) {
  auto &item = items[row * column_count + col];
  item.valid = true;
  if (item.val != val || item.bg_color != color) {
    item.val = val;
    item.bg_color = color;
    auto idx = index(row, col);
    emit dataChanged(idx, idx, {Qt::DisplayRole});
  }
}

void MessageBytesModel::updateState() {
  const auto *last_msg = can->snapshot(msg_id);
  const auto &binary = last_msg->dat;
  // Handle size changes in binary data
  if (binary.size() > row_count) {
    beginInsertRows({}, row_count, binary.size() - 1);
    row_count = binary.size();
    items.resize(row_count * column_count);
    endInsertRows();
  }

  auto &bit_flips = heatmap_live_mode ? last_msg->bit_flip_counts : getBitFlipChanges(binary.size());
  // Find the maximum bit flip count across the message
  uint32_t max_bit_flip_count = 1;  // Default to 1 to avoid division by zero
  for (const auto &row : bit_flips) {
    for (uint32_t count : row) {
      max_bit_flip_count = std::max(max_bit_flip_count, count);
    }
  }

  const double max_alpha = 255.0;
  const double min_alpha_with_signal = 25.0;  // Base alpha for small flip counts
  const double min_alpha_no_signal = 10.0;    // Base alpha for small flip counts for no signal bits
  const double log_factor = 1.0 + 0.2;        // Factor for logarithmic scaling
  const double log_scaler = max_alpha / log2(log_factor * max_bit_flip_count);

  for (size_t i = 0; i < binary.size(); ++i) {
    for (int j = 0; j < 8; ++j) {
      auto &item = items[i * column_count + j];
      int bit_val = (binary[i] >> (7 - j)) & 1;

      double alpha = item.sigs.empty() ? 0 : min_alpha_with_signal;
      uint32_t flip_count = bit_flips[i][j];
      if (flip_count > 0) {
        double normalized_alpha = log2(1.0 + flip_count * log_factor) * log_scaler;
        double min_alpha = item.sigs.empty() ? min_alpha_no_signal : min_alpha_with_signal;
        alpha = std::clamp(normalized_alpha, min_alpha, max_alpha);
      }

      auto color = item.bg_color;
      color.setAlpha(alpha);
      updateItem(i, j, bit_val, color);
    }
    updateItem(i, 8, binary[i], last_msg->colors[i]);
  }
}

const std::vector<std::array<uint32_t, 8>> &MessageBytesModel::getBitFlipChanges(size_t msg_size) {
  // Return cached results if time range and data are unchanged
  auto time_range = can->timeRange();
  if (bit_flip_tracker.time_range == time_range && !bit_flip_tracker.flip_counts.empty())
    return bit_flip_tracker.flip_counts;

  bit_flip_tracker.time_range = time_range;
  bit_flip_tracker.flip_counts.assign(msg_size, std::array<uint32_t, 8>{});

  // Iterate over events within the specified time range and calculate bit flips
  auto [first, last] = can->eventsInRange(msg_id, time_range);
  if (std::distance(first, last) <= 1) return bit_flip_tracker.flip_counts;

  std::vector<uint8_t> prev_values((*first)->dat, (*first)->dat + (*first)->size);
  for (auto it = std::next(first); it != last; ++it) {
    const CanEvent *event = *it;
    int size = std::min<int>(msg_size, event->size);
    for (int i = 0; i < size; ++i) {
      const uint8_t diff = event->dat[i] ^ prev_values[i];
      if (!diff) continue;

      auto &bit_flips = bit_flip_tracker.flip_counts[i];
      for (int bit = 0; bit < 8; ++bit) {
        if (diff & (1u << bit)) ++bit_flips[7 - bit];
      }
      prev_values[i] = event->dat[i];
    }
  }

  return bit_flip_tracker.flip_counts;
}

QVariant MessageBytesModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation == Qt::Vertical) {
    switch (role) {
      case Qt::DisplayRole: return section;
      case Qt::SizeHintRole: return QSize(VERTICAL_HEADER_WIDTH, 0);
      case Qt::TextAlignmentRole: return Qt::AlignCenter;
    }
  }
  return {};
}

QVariant MessageBytesModel::data(const QModelIndex &index, int role) const {
  auto item = (const MessageBytesModel::Item *)index.internalPointer();
  return role == Qt::ToolTipRole && item && !item->sigs.empty() ? signalToolTip(item->sigs.back()) : QVariant();
}
