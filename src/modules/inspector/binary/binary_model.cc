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

void BinaryModel::setMessage(const MessageId& message_id) {
  msg_id = message_id;
  rebuild();
}

void BinaryModel::setHeatmapMode(bool live) {
  heatmap_live_mode = live;
  updateState();
}

void BinaryModel::rebuild() {
  beginResetModel();
  initializeItems();

  auto dbc_msg = GetDBC()->msg(msg_id);
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

  auto snapshot = StreamManager::stream()->snapshot(msg_id);
  auto dbc_msg = GetDBC()->msg(msg_id);

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

      item.sigs.push_back(sig);

      // Sort overlapping signals: Smallest (inner) signals last
      // so they are prioritized for hover/interaction
      if (item.sigs.size() > 1) {
        std::ranges::sort(item.sigs, std::ranges::greater{}, &dbc::Signal::size);
      }
    }
  }
}

void BinaryModel::updateBorders() {
  for (int r = 0; r < row_count; ++r) {
    for (int c = 0; c < column_count; ++c) {
      auto& item = items[r * column_count + c];
      if (item.sigs.isEmpty()) {
        item.borders = {};
        continue;
      }

      auto matches = [&](int nr, int nc) {
        if (nr < 0 || nr >= row_count || nc < 0 || nc >= column_count) return false;
        return items[nr * column_count + nc].sigs == item.sigs;
      };

      item.borders.left = !matches(r, c - 1);
      item.borders.right = !matches(r, c + 1);
      item.borders.top = !matches(r - 1, c);
      item.borders.bottom = !matches(r + 1, c);

      item.borders.top_left = !matches(r - 1, c - 1);
      item.borders.top_right = !matches(r - 1, c + 1);
      item.borders.bottom_left = !matches(r + 1, c - 1);
      item.borders.bottom_right = !matches(r + 1, c + 1);
    }
  }
}

bool BinaryModel::updateItem(int row, int col, uint8_t val, const QColor& color) {
  auto& item = items[row * column_count + col];
  if (item.val != val || item.bg_color != color) {
    item.val = val;
    item.bg_color = color;
    return true;
  }
  return false;
}

void BinaryModel::updateState() {
  const auto* last_msg = StreamManager::stream()->snapshot(msg_id);
  const size_t msg_size = last_msg->size;
  if (msg_size == 0) {
    for (auto& item : items) {
      item.val = INVALID_BIT;
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

  const auto& bit_flips = heatmap_live_mode ? last_msg->bit_flips : getBitFlipChanges(msg_size);

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
    if (syncRowItems(i, last_msg, bit_flips[i], log_max, is_light_theme, base_bg, decay_factor)) {
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
    if (items[i].sigs.contains(sig)) {
      auto index = this->index(i / column_count, i % column_count);
      emit dataChanged(index, index, {Qt::DisplayRole});
    }
  }
}

QSet<const dbc::Signal*> BinaryModel::getOverlappingSignals() const {
  QSet<const dbc::Signal*> overlapping;
  for (const auto& item : items) {
    if (item.sigs.size() > 1) {
      for (auto s : item.sigs) {
        if (s->type == dbc::Signal::Type::Normal) overlapping += s;
      }
    }
  }
  return overlapping;
}

bool BinaryModel::syncRowItems(int row, const MessageSnapshot* msg, const std::array<uint32_t, 8>& row_flips,
                               float log_max, bool is_light, const QColor& base_bg, float decay) {
  bool row_dirty = false;
  const uint8_t byte_val = msg->data[row];
  const size_t row_offset = row * column_count;

  // Update 8 Bit Columns
  for (int j = 0; j < 8; ++j) {
    auto& item = items[row_offset + j];
    const int bit_val = (byte_val >> (7 - j)) & 1;

    QColor heat_color = calculateBitHeatColor(item, row_flips[j], log_max, is_light, base_bg, decay);
    row_dirty |= updateItem(row, j, bit_val, heat_color);
  }

  // Update 9th Column (Hex Value)
  QColor byte_color = QColor::fromRgba(msg->colors[row]);
  row_dirty |= updateItem(row, 8, byte_val, byte_color);

  return row_dirty;
}

QColor BinaryModel::calculateBitHeatColor(Item& item, uint32_t flips, float log_max, bool is_light,
                                          const QColor& base_bg, float decay_factor) {
  float target = std::clamp(std::log2(static_cast<float>(flips) + 1.0f) / log_max, 0.0f, 1.0f);
  if (heatmap_live_mode) {
    if (flips != item.last_flips) {
      item.intensity = std::max(item.intensity, target);
      item.last_flips = flips;
    } else {
      item.intensity *= decay_factor;  // Smoothly fade out
    }
  } else {
    item.intensity = target;  // Direct mapping for static range view
  }

  const float i = item.intensity;
  const bool is_in_signal = !item.sigs.empty();

  // 2. Signal Coloring Logic (Hue Preservation)
  if (is_in_signal) {
    QColor c = item.sigs.back()->color;

    // Minimum Alpha Floor (100) so signals are always identifiable
    // Maximum Alpha (255) for high activity
    int alpha = static_cast<int>(100 + (155 * i));

    if (i > 0.05f) {
      int h, s, v, a;
      c.getHsv(&h, &s, &v, &a);
      // Boost Value (Brightness) and Saturation slightly based on heat
      // Using HSV prevents the "whitening" caused by HSL lighter()
      v = std::min(255, v + static_cast<int>(50 * i));
      s = std::min(255, s + static_cast<int>(20 * i));
      c.setHsv(h, s, v, alpha);
    } else {
      c.setAlpha(alpha);
    }
    return c;
  }

  // 3. Empty Space Heatmap Logic (Background -> Red)
  if (i < 0.01f) return Qt::transparent;

  QColor hot = is_light ? QColor(255, 0, 0) : QColor(255, 80, 80);
  float inv_i = 1.0f - i;
  int min_alpha = is_light ? 40 : 60;

  return QColor(static_cast<int>(base_bg.red() * inv_i + hot.red() * i),
                static_cast<int>(base_bg.green() * inv_i + hot.green() * i),
                static_cast<int>(base_bg.blue() * inv_i + hot.blue() * i),
                static_cast<int>(min_alpha * inv_i + 220 * i));
}

const std::array<std::array<uint32_t, 8>, MAX_CAN_LEN>& BinaryModel::getBitFlipChanges(size_t msg_size) {
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
  auto [first, last] = stream->eventsInRange(msg_id, time_range);
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
  auto item = (const BinaryModel::Item*)index.internalPointer();
  return role == Qt::ToolTipRole && item && !item->sigs.empty() ? signalToolTip(item->sigs.back()) : QVariant();
}

QString signalToolTip(const dbc::Signal* sig) {
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
