#include "history_model.h"

#include <functional>

#include "core/dbc/dbc_manager.h"
#include "modules/message_list/message_delegate.h"
#include "modules/system/stream_manager.h"
#include "utils/util.h"

static const size_t LIVE_VIEW_LIMIT = 500;

QVariant MessageHistoryModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= static_cast<int>(messages.size())) return {};

  const auto& m = messages[index.row()];
  const int col = index.column();

  if (role == Qt::DisplayRole) {
    if (col == 0) return QString::number(StreamManager::stream()->toSeconds(m.mono_ns), 'f', 3);
    if (isHexMode()) return {};  // Handled by delegate

    const int sig_idx = col - 1;
    if (sig_idx < (int)m.sig_values.size()) {
      return sigs[sig_idx].sig->formatValue(m.sig_values[sig_idx], false);
    }
  } else if (role == ColumnTypeRole::IsHexColumn) {
    return isHexMode() && col == 1;
  }
  return {};
}

void MessageHistoryModel::setMessage(const MessageId& message_id) {
  msg_id = message_id;
  rebuild();
}

void MessageHistoryModel::setPauseState(bool paused) {
  if (is_paused == paused) return;
  is_paused = paused;

  if (!is_paused) {
    // Transitioning back to Live: Prune the list to the live limit immediately
    pruneToLiveLimit();
    updateState(false);
  }
}

void MessageHistoryModel::rebuild() {
  beginResetModel();
  sigs.clear();
  if (auto dbc_msg = GetDBC()->msg(msg_id)) {
    for (auto* s : dbc_msg->getSignals()) {
      QString display_name = QString(s->name).replace('_', ' ');
      sigs.push_back({display_name, s});
    }
  }
  messages.clear();
  hex_colors = {};
  endResetModel();
  setFilter(0, "", nullptr);
}

QVariant MessageHistoryModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || section < 0) return {};

  if (section == 0) {
    if (role == Qt::DisplayRole) return "Time";
    if (role == Qt::ToolTipRole) return tr("Arrival time in seconds");
    return {};
  }

  if (section - 1 >= static_cast<int>(sigs.size())) return {};
  const auto& col = sigs[section - 1];
  const bool hex = isHexMode();

  switch (role) {
    case Qt::DisplayRole: return hex ? "Data" : col.display_name;
    case Qt::BackgroundRole: return (!hex) ? col.sig->color : QVariant();
    case Qt::ToolTipRole:
      if (hex) return tr("Raw message data (Hex)");
      return col.sig->unit.isEmpty() ? col.sig->name : QString("%1 (%2)").arg(col.sig->name, col.sig->unit);
    default: return {};
  }
}

void MessageHistoryModel::setHexMode(bool hex) {
  if (hex_mode == hex) return;
  hex_mode = hex;
  rebuild();
}

void MessageHistoryModel::setFilter(int sig_idx, const QString& value, std::function<bool(double, double)> cmp) {
  filter_sig_idx = sig_idx;
  filter_value = value.toDouble();
  filter_cmp = value.isEmpty() ? nullptr : cmp;
  updateState(true);
}

void MessageHistoryModel::updateState(bool clear) {
  if (clear && !messages.empty()) {
    beginRemoveRows({}, 0, messages.size() - 1);
    messages.clear();
    hex_colors = {};
    endRemoveRows();
  }

  auto* stream = StreamManager::stream();
  const auto* snapshot = stream ? stream->snapshot(msg_id) : nullptr;
  if (!snapshot) return;
  uint64_t current_time = stream->toMonoNs(snapshot->ts) + 1;
  uint64_t last_time = messages.empty() ? 0 : messages.front().mono_ns;

  // Insert at index 0 (top of the list)
  fetchData(0, current_time, last_time);

  if (!is_paused) pruneToLiveLimit();
}

bool MessageHistoryModel::canFetchMore(const QModelIndex& parent) const {
  Q_UNUSED(parent);
  // Strategy: Only allow fetching older history when paused to prevent list jumps
  if (!is_paused || messages.empty()) return false;

  const auto& events = StreamManager::stream()->events(msg_id);
  if (events.empty()) return false;

  return messages.back().mono_ns > events.front()->mono_ns;
}

void MessageHistoryModel::fetchMore(const QModelIndex& parent) {
  Q_UNUSED(parent);
  if (messages.empty()) return;
  // Fetch older data at the end (Infinite Scroll)
  fetchData(static_cast<int>(messages.size()), messages.back().mono_ns, 0);
}

void MessageHistoryModel::pruneToLiveLimit() {
  if (messages.size() > LIVE_VIEW_LIMIT) {
    beginRemoveRows({}, static_cast<int>(LIVE_VIEW_LIMIT), static_cast<int>(messages.size()) - 1);
    messages.erase(messages.begin() + LIVE_VIEW_LIMIT, messages.end());
    endRemoveRows();
  }
}

void MessageHistoryModel::fetchData(int insert_pos_idx, uint64_t from_time, uint64_t min_time) {
  auto* stream = StreamManager::stream();
  if (!stream) return;
  const auto& events = stream->events(msg_id);
  if (events.empty()) return;

  auto first =
      std::lower_bound(events.rbegin(), events.rend(), from_time, [](auto e, uint64_t ts) { return e->mono_ns > ts; });

  const bool in_hex_mode = isHexMode();
  const int sig_count = static_cast<int>(sigs.size());
  const bool has_filter = filter_cmp && filter_sig_idx >= 0 && filter_sig_idx < sig_count;

  std::vector<MessageHistoryModel::LogEntry> msgs;
  msgs.reserve(batch_size);
  for (; first != events.rend(); ++first) {
    const CanEvent* e = *first;
    if (e->mono_ns <= min_time) break;

    // Parse filter signal first — reject before any further work
    double filter_parsed = 0.0;
    if (has_filter) {
      filter_parsed = sigs[filter_sig_idx].sig->parse(e->dat, e->size).value_or(0);
      if (!filter_cmp(filter_parsed, filter_value)) continue;
    }

    LogEntry entry;
    entry.mono_ns = e->mono_ns;
    entry.size = e->size;
    std::copy_n(e->dat, std::min<int>(e->size, MAX_CAN_LEN), entry.data.begin());

    // Skip signal decoding entirely in hex mode — delegate renders raw bytes
    if (!in_hex_mode) {
      entry.sig_values.resize(sig_count);
      for (int i = 0; i < sig_count; ++i) {
        entry.sig_values[i] = (has_filter && i == filter_sig_idx)
            ? filter_parsed
            : sigs[i].sig->parse(e->dat, e->size).value_or(0);
      }
    }

    msgs.push_back(std::move(entry));
    if (msgs.size() >= batch_size && min_time == 0) break;
  }

  if (!msgs.empty()) {
    if (in_hex_mode && (min_time > 0 || messages.empty())) {
      const auto* snapshot = stream->snapshot(msg_id);
      if (!snapshot) return;
      const auto freq = snapshot->freq;
      const bool is_dark = utils::isDarkTheme();
      for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) {
        double ts = it->mono_ns / 1e9;
        hex_colors.update(it->data.data(), it->size, ts, freq);
        for (int i = 0; i < it->size; ++i) {
          auto info = hex_colors.bytePattern(i);
          it->colors[i] = colorFromDataPattern(info.pattern, ts, info.last_change_ts, freq, is_dark);
        }
      }
    }

    beginInsertRows({}, insert_pos_idx, insert_pos_idx + msgs.size() - 1);
    // push_front/push_back
    if (insert_pos_idx == 0) {
      for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) messages.push_front(std::move(*it));
    } else {
      for (auto& m : msgs) messages.push_back(std::move(m));
    }
    endInsertRows();
  }
}
