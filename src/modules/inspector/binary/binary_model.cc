#include "binary_model.h"

#include <QApplication>
#include <QDebug>
#include <QPalette>
#include <algorithm>
#include <cmath>

#include "core/commands/commands.h"
#include "core/streams/message_state.h"
#include "modules/settings/settings.h"
#include "modules/system/stream_manager.h"

BinaryModel::BinaryModel(QObject* parent) : QAbstractTableModel(parent) {
  header_font_ = QApplication::font();
  header_font_.setPointSize(9);
  header_font_.setBold(true);

  connect(GetDBC(), &dbc::Manager::DBCFileChanged, this, &BinaryModel::rebuild);
  connect(UndoStack::instance(), &QUndoStack::indexChanged, this, &BinaryModel::rebuild);
}

const BinaryModel::Item* BinaryModel::getItem(const QModelIndex& index) const {
  if (!index.isValid()) return nullptr;
  int idx = index.row() * column_count + index.column();
  return (idx >= 0 && idx < (int)items.size()) ? &items[idx] : nullptr;
}

void BinaryModel::setMessage(const MessageId& id) {
  message_id = id;
  rebuild();
}

void BinaryModel::setHeatmapMode(bool live) {
  heatmap_live_mode = live;
  updateState();
}

void BinaryModel::rebuild() {
  beginResetModel();
  initializeItems();

  auto dbc_msg = GetDBC()->msg(message_id);
  if (dbc_msg) {
    mapSignalsToItems(dbc_msg);
  }

  updateBorders();
  endResetModel();
  updateState();
}

void BinaryModel::initializeItems() {
  bit_flip_tracker = {};
  items.clear();

  auto snapshot = StreamManager::stream()->snapshot(message_id);
  auto dbc_msg = GetDBC()->msg(message_id);

  // Prefer DBC size, fallback to message snapshot size
  row_count = dbc_msg ? dbc_msg->size : (snapshot ? snapshot->size : 8);
  items.resize(row_count * column_count);
}

void BinaryModel::mapSignalsToItems(const dbc::Msg* msg) {
  for (auto sig : msg->getSignals()) {
    for (int j = 0; j < sig->size; ++j) {
      int abs_bit = sig->getBitIndex(j);
      int row = abs_bit / 8;
      int col = 7 - (abs_bit % 8);  // Physical grid: Col 0 is Bit 7
      int idx = row * column_count + col;

      if (idx >= items.size()) {
        qWarning() << "Signal" << sig->name << "out of bounds at bit" << abs_bit;
        break;
      }

      auto& item = items[idx];
      item.is_lsb |= (abs_bit == sig->lsb);
      item.is_msb |= (abs_bit == sig->msb);
      item.bg_color = sig->color;  // Last signal in list sets the primary color
      item.bg_color.setAlpha(100);

      item.signal_list.push_back(sig);

      // Sort overlapping signals: Smallest (inner) signals last
      // so they are prioritized for hover/interaction
      if (item.signal_list.size() > 1) {
        std::ranges::sort(item.signal_list, std::ranges::greater{}, &dbc::Signal::size);
      }
    }
  }
}

void BinaryModel::updateBorders() {
  for (int row = 0; row < row_count; ++row) {
    for (int col = 0; col < column_count; ++col) {
      auto& item = items[row * column_count + col];
      if (item.signal_list.isEmpty()) {
        item.borders = {};
        continue;
      }

      auto matches = [&](int neighbor_row, int neighbor_col) {
        if (neighbor_row < 0 || neighbor_row >= row_count || neighbor_col < 0 || neighbor_col >= column_count) return false;
        return items[neighbor_row * column_count + neighbor_col].signal_list == item.signal_list;
      };

      item.borders.left = !matches(row, col - 1);
      item.borders.right = !matches(row, col + 1);
      item.borders.top = !matches(row - 1, col);
      item.borders.bottom = !matches(row + 1, col);

      item.borders.top_left = !matches(row - 1, col - 1);
      item.borders.top_right = !matches(row - 1, col + 1);
      item.borders.bottom_left = !matches(row + 1, col - 1);
      item.borders.bottom_right = !matches(row + 1, col + 1);
    }
  }
}

bool BinaryModel::updateItem(int row, int col, uint8_t val, const QColor& color) {
  auto& item = items[row * column_count + col];
  if (item.value != val || item.bg_color != color) {
    item.value = val;
    item.bg_color = color;
    return true;
  }
  return false;
}

void BinaryModel::updateState() {
  const auto* last_msg = StreamManager::stream()->snapshot(message_id);
  const size_t msg_size = last_msg->size;
  if (msg_size == 0) {
    for (auto& item : items) {
      item.value = INVALID_BIT;
    }
    emit dataChanged(index(0, 0), index(row_count - 1, column_count - 1), {Qt::DisplayRole});
    return;
  }

  if (msg_size > row_count) {
    beginInsertRows({}, row_count, msg_size - 1);
    row_count = msg_size;
    items.resize(row_count * column_count);
    endInsertRows();
  }

  const bool is_light_theme = !utils::isDarkTheme();
  const QColor base_bg = qApp->palette().color(QPalette::Base);
  const float fps = std::max(1.0f, static_cast<float>(settings.fps));

  // Adaptive Decay Calculation
  float decay_factor = 0.95f;
  if (heatmap_live_mode && last_msg->freq > 0) {
    // Calculate per-frame decay to maintain "heat" for ~2 message periods (capped 0.5s–2.0s)
    // factor = 0.1 ^ (1 / (FPS * duration))
    float persistence = std::clamp(2.0f / static_cast<float>(last_msg->freq), 0.5f, 2.0f);
    decay_factor = std::pow(0.1f, 1.0f / (fps * persistence));
  }

  const auto& bit_flips = heatmap_live_mode ? last_msg->bit_flips : computeBitFlipCounts(msg_size);

  // Find max flips for relative scaling
  uint32_t max_flips = 1;
  for (size_t i = 0; i < msg_size; ++i) {
    for (uint32_t count : bit_flips[i]) {
      max_flips = std::max(max_flips, count);
    }
  }

  const float log_max = std::log2(static_cast<float>(max_flips) + 1.0f);
  int first_dirty = -1, last_dirty = -1;

  for (size_t i = 0; i < msg_size; ++i) {
    if (updateRowCells(i, last_msg, bit_flips[i], last_msg->mask[i], log_max, is_light_theme, base_bg, decay_factor)) {
      if (first_dirty == -1) first_dirty = i;
      last_dirty = i;
    }
  }

  if (first_dirty != -1) {
    emit dataChanged(index(first_dirty, 0), index(last_dirty, 8), {Qt::DisplayRole});
  }
}

void BinaryModel::updateSignalCells(const dbc::Signal* sig) {
  for (int i = 0; i < items.size(); ++i) {
    if (items[i].signal_list.contains(sig)) {
      auto index = this->index(i / column_count, i % column_count);
      emit dataChanged(index, index, {Qt::DisplayRole});
    }
  }
}

QSet<const dbc::Signal*> BinaryModel::findOverlappingSignals() const {
  QSet<const dbc::Signal*> overlapping;
  for (const auto& item : items) {
    if (item.signal_list.size() > 1) {
      for (auto s : item.signal_list) {
        if (s->type == dbc::Signal::Type::Normal) overlapping += s;
      }
    }
  }
  return overlapping;
}

bool BinaryModel::updateRowCells(int row, const MessageSnapshot* msg, const std::array<uint32_t, 8>& row_flips,
                               uint8_t byte_mask, float log_max, bool is_light_theme, const QColor& base_bg, float decay_factor) {
  bool row_dirty = false;
  const uint8_t byte_val = msg->data[row];
  const size_t row_offset = row * column_count;

  // Update 8 Bit Columns
  for (int j = 0; j < 8; ++j) {
    auto& item = items[row_offset + j];
    const int bit_val = (byte_val >> (7 - j)) & 1;
    const bool is_masked = (byte_mask >> (7 - j)) & 1;

    QColor heat_color = calculateBitHeatColor(item, row_flips[j], is_masked, log_max, is_light_theme, base_bg, decay_factor);
    row_dirty |= updateItem(row, j, bit_val, heat_color);
  }

  // Update 9th Column (Hex Value)
  QColor byte_color = QColor::fromRgba(msg->colors[row]);
  row_dirty |= updateItem(row, 8, byte_val, byte_color);

  return row_dirty;
}

QColor BinaryModel::calculateBitHeatColor(Item& item, uint32_t flips, bool is_masked, float log_max, bool is_light_theme,
                                          const QColor& base_bg, float decay_factor) {
  // Update intensity tracking (even when masked, to avoid stale flash on unmute)
  if (is_masked) {
    item.last_flips = flips;
    item.intensity = 0.0f;
  } else if (heatmap_live_mode) {
    if (flips != item.last_flips) {
      item.intensity = 1.0f;
      item.last_flips = flips;
    } else {
      item.intensity *= decay_factor;
    }
  } else {
    item.intensity = std::clamp(std::log2(static_cast<float>(flips) + 1.0f) / log_max, 0.0f, 1.0f);
  }

  const float i = item.intensity;

  // Signal bits: base color with alpha [100..255] and HSV boost proportional to heat
  if (!item.signal_list.empty()) {
    int h, s, v, a;
    item.signal_list.back()->color.getHsv(&h, &s, &v, &a);
    v = std::min(255, v + static_cast<int>(50 * i));
    s = std::min(255, s + static_cast<int>(20 * i));
    return QColor::fromHsv(h, s, v, static_cast<int>(100 + 155 * i));
  }

  // Unsignaled bits: background -> hot color blend
  if (i < 0.01f) return Qt::transparent;

  const QColor hot = is_light_theme ? QColor(255, 0, 0) : QColor(255, 80, 80);
  const float inv_i = 1.0f - i;
  const int min_alpha = is_light_theme ? 40 : 60;
  return QColor(static_cast<int>(base_bg.red() * inv_i + hot.red() * i),
                static_cast<int>(base_bg.green() * inv_i + hot.green() * i),
                static_cast<int>(base_bg.blue() * inv_i + hot.blue() * i),
                static_cast<int>(min_alpha * inv_i + 220 * i));
}

const std::array<std::array<uint32_t, 8>, MAX_CAN_LEN>& BinaryModel::computeBitFlipCounts(size_t msg_size) {
  // Return cached results if time range and data are unchanged
  auto* stream = StreamManager::stream();
  auto time_range = stream->timeRange();
  if (!time_range) {
    time_range = {stream->minSeconds(), stream->maxSeconds()};
  }

  if (bit_flip_tracker.time_range == time_range && !bit_flip_tracker.flip_counts.empty())
    return bit_flip_tracker.flip_counts;

  bit_flip_tracker.time_range = time_range;
  bit_flip_tracker.flip_counts.fill({});

  // Iterate over events within the specified time range and calculate bit flips
  auto [first, last] = stream->eventsInRange(message_id, time_range);
  if (std::distance(first, last) <= 1) return bit_flip_tracker.flip_counts;

  std::vector<uint8_t> prev_values((*first)->dat, (*first)->dat + (*first)->size);
  for (auto it = std::next(first); it != last; ++it) {
    const CanEvent* event = *it;
    int size = std::min<int>(msg_size, event->size);
    for (int i = 0; i < size; ++i) {
      const uint8_t diff = event->dat[i] ^ prev_values[i];
      if (!diff) continue;

      auto& bit_flips = bit_flip_tracker.flip_counts[i];
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
      case Qt::DisplayRole: return section;
      case Qt::SizeHintRole: return QSize(CELL_WIDTH, CELL_HEIGHT);
      case Qt::TextAlignmentRole: return Qt::AlignCenter;
      case Qt::FontRole: return header_font_;
    }
  }
  return {};
}

QVariant BinaryModel::data(const QModelIndex& index, int role) const {
  if (role == Qt::ToolTipRole) {
    const auto *item = getItem(index);
    return item && !item->signal_list.empty() ? formatSignalToolTip(item->signal_list.back()) : QVariant();
  }
  return QVariant();
}

QString formatSignalToolTip(const dbc::Signal* sig) {
  return QObject::tr(R"(
    %1<br /><span font-size:small">
    Start Bit: %2 Size: %3<br />
    MSB: %4 LSB: %5<br />
    Little Endian: %6 Signed: %7</span>
  )")
      .arg(sig->name)
      .arg(sig->start_bit)
      .arg(sig->size)
      .arg(sig->msb)
      .arg(sig->lsb)
      .arg(sig->is_little_endian ? "Y" : "N")
      .arg(sig->is_signed ? "Y" : "N");
}
