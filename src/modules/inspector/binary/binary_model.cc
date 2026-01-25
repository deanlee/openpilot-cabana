#include "binary_model.h"

#include <QApplication>
#include <QDebug>
#include <QPalette>
#include <algorithm>
#include <cmath>

#include "core/streams/message_state.h"
#include "modules/settings/settings.h"
#include "modules/system/stream_manager.h"

void BinaryModel::refresh() {
  beginResetModel();
  bit_flip_tracker = {};
  items.clear();
  if (auto dbc_msg = GetDBC()->msg(msg_id)) {
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
    row_count = StreamManager::stream()->snapshot(msg_id)->dat.size();
    items.resize(row_count * column_count);
  }
  updateBorders();
  endResetModel();
  updateState();
}

void BinaryModel::updateBorders() {
  for (int r = 0; r < row_count; ++r) {
    for (int c = 0; c < column_count; ++c) {
      auto &item = items[r * column_count + c];
      if (item.sigs.isEmpty()) {
        item.borders = {};
        continue;
      }

      auto matches = [&](int nr, int nc) {
        if (nr < 0 || nr >= row_count || nc < 0 || nc >= column_count) return false;
        return items[nr * column_count + nc].sigs == item.sigs;
      };

      item.borders.left   = !matches(r, c - 1);
      item.borders.right  = !matches(r, c + 1);
      item.borders.top    = !matches(r - 1, c);
      item.borders.bottom = !matches(r + 1, c);

      item.borders.top_left     = !matches(r - 1, c - 1);
      item.borders.top_right    = !matches(r - 1, c + 1);
      item.borders.bottom_left  = !matches(r + 1, c - 1);
      item.borders.bottom_right = !matches(r + 1, c + 1);
    }
  }
}

bool BinaryModel::updateItem(int row, int col, uint8_t val, const QColor &color) {
  auto &item = items[row * column_count + col];
  item.valid = true;
  if (item.val != val || item.bg_color != color) {
    item.val = val;
    item.bg_color = color;
    return true;
  }
  return false;
}

void BinaryModel::updateState() {
  const auto* last_msg = StreamManager::stream()->snapshot(msg_id);
  const auto& binary = last_msg->dat;

  if (binary.size() > row_count) {
    beginInsertRows({}, row_count, binary.size() - 1);
    row_count = binary.size();
    items.resize(row_count * column_count);
    endInsertRows();
  }

  auto& bit_flips = heatmap_live_mode ? last_msg->bit_flips : getBitFlipChanges(binary.size());

  // Find max flips for relative scaling
  uint32_t max_bit_flip_count = 1;
  for (const auto& row : bit_flips) {
    for (uint32_t count : row) {
      max_bit_flip_count = std::max(max_bit_flip_count, count);
    }
  }

  const double current_sec = StreamManager::stream()->currentSec();
  const bool is_light_theme = !utils::isDarkTheme();
  const QColor base_bg = qApp->palette().color(QPalette::Base);
  const float log_max = std::log2(static_cast<float>(max_bit_flip_count) + 1.0f);
  int first_dirty = -1, last_dirty = -1;

  for (size_t i = 0; i < binary.size(); ++i) {
    bool row_changed = false;
    const uint8_t byte_val = (uint8_t)binary[i];
    for (int j = 0; j < 8; ++j) {
      auto& item = items[i * column_count + j];
      int bit_val = (byte_val >> (7 - j)) & 1;

      // Calculate color based on heat
      QColor bit_color = calculateBitHeatColor(item, bit_flips[i][j], log_max, is_light_theme, base_bg);
      row_changed |= updateItem(i, j, bit_val, bit_color);
    }

    // The 9th column (index 8) remains the Byte Value with the Trend Color
    row_changed |= updateItem(i, 8, byte_val, last_msg->getPatternColor(i, current_sec));

    if (row_changed) {
      if (first_dirty == -1) first_dirty = i;
      last_dirty = i;
    }
  }

  if (first_dirty != -1) {
    emit dataChanged(index(first_dirty, 0), index(last_dirty, 8), {Qt::DisplayRole});
  }
}

QColor BinaryModel::calculateBitHeatColor(Item& item, uint32_t flips, float log_max,
                                          bool is_light, const QColor& base_bg) {
  const bool is_in_signal = !item.sigs.empty();

  if (flips == 0) {
    item.intensity = 0.0f;
    item.last_flips = 0;
    if (!is_in_signal) return Qt::transparent;

    QColor c = item.sigs.back()->color;
    c.setAlpha(is_light ? 45 : 30);
    return c;
  }

  // Calculate Heat using Log2 for a smooth UI transition
  if (flips != item.last_flips) {
    float target = std::clamp(std::log2(static_cast<float>(flips) + 1.0f) / log_max, 0.0f, 1.0f);
    item.intensity = std::max(item.intensity, target);
    item.last_flips = flips;
  } else {
    item.intensity *= 0.95f;
  }

  float i = item.intensity;

  if (is_in_signal) {
    // 100% Signal Color Respect: Only modify Alpha and Brightness
    QColor c = item.sigs.back()->color;
    c.setAlpha(static_cast<int>(100 + (155 * i)));

    // Use .lighter() for glow to avoid shifting hue to Red/White
    return (i > 0.6f) ? c.lighter(100 + static_cast<int>(40 * (i - 0.6f))) : c;
  } else {
    // Standard Heatmap for empty space: Blend Window Background -> Red/Coral
    QColor hot = is_light ? Qt::red : QColor(255, 100, 100);
    float inv_i = 1.0f - i;

    return QColor(
        static_cast<int>(base_bg.red() * inv_i + hot.red() * i),
        static_cast<int>(base_bg.green() * inv_i + hot.green() * i),
        static_cast<int>(base_bg.blue() * inv_i + hot.blue() * i),
        static_cast<int>((is_light ? 40 : 60) * inv_i + 220 * i));
  }
}

const std::vector<std::array<uint32_t, 8>> &BinaryModel::getBitFlipChanges(size_t msg_size) {
  // Return cached results if time range and data are unchanged
  auto time_range = StreamManager::stream()->timeRange();
  if (bit_flip_tracker.time_range == time_range && !bit_flip_tracker.flip_counts.empty())
    return bit_flip_tracker.flip_counts;

  bit_flip_tracker.time_range = time_range;
  bit_flip_tracker.flip_counts.assign(msg_size, std::array<uint32_t, 8>{});

  // Iterate over events within the specified time range and calculate bit flips
  auto [first, last] = StreamManager::stream()->eventsInRange(msg_id, time_range);
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

QVariant BinaryModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation == Qt::Vertical) {
    switch (role) {
      case Qt::DisplayRole:
        return section;

      case Qt::SizeHintRole:
        return QSize(CELL_WIDTH, CELL_HEIGHT);

      case Qt::TextAlignmentRole:
        return Qt::AlignCenter;

      case Qt::FontRole: {
        QFont font;
        font.setPointSize(9);
        font.setBold(true);
        return font;
      }
    }
  }
  return {};
}
QVariant BinaryModel::data(const QModelIndex &index, int role) const {
  auto item = (const BinaryModel::Item *)index.internalPointer();
  return role == Qt::ToolTipRole && item && !item->sigs.empty() ? signalToolTip(item->sigs.back()) : QVariant();
}

QString signalToolTip(const dbc::Signal *sig) {
  return QObject::tr(R"(
    %1<br /><span font-size:small">
    Start Bit: %2 Size: %3<br />
    MSB: %4 LSB: %5<br />
    Little Endian: %6 Signed: %7</span>
  )").arg(sig->name).arg(sig->start_bit).arg(sig->size).arg(sig->msb).arg(sig->lsb)
     .arg(sig->is_little_endian ? "Y" : "N").arg(sig->is_signed ? "Y" : "N");
}
