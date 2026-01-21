#include "history_model.h"

#include <functional>

#include "core/dbc/dbc_manager.h"
#include "modules/message_list/message_delegate.h"
#include "modules/system/stream_manager.h"

static const size_t LIVE_VIEW_LIMIT = 500;

QVariant MessageHistoryModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= (int)messages.size()) return {};

  const auto& m = messages[index.row()];
  const int col = index.column();

  if (role == Qt::DisplayRole) {
    if (col == 0) return QString::number(StreamManager::stream()->toSeconds(m.mono_time), 'f', 3);
    if (isHexMode()) return {};  // Handled by delegate

    if (col - 1 < (int)m.sig_values.size()) {
      return sigs[col - 1]->formatValue(m.sig_values[col - 1], false);
    };
  } else if (role == Qt::TextAlignmentRole) {
    return (uint32_t)(Qt::AlignRight | Qt::AlignVCenter);
  } else if (role == ColumnTypeRole::IsHexColumn) {
    return isHexMode() && index.column() == 1;
  }
  return {};
}

void MessageHistoryModel::setMessage(const MessageId &message_id) {
  msg_id = message_id;
  reset();
}

void MessageHistoryModel::setPauseState(bool paused) {
  if (is_paused == paused) return;
  is_paused = paused;

  if (!is_paused) {
    // Transitioning back to Live: Prune the list to the live limit immediately
    if (messages.size() > LIVE_VIEW_LIMIT) {
      beginRemoveRows({}, LIVE_VIEW_LIMIT, messages.size() - 1);
      messages.erase(messages.begin() + LIVE_VIEW_LIMIT, messages.end());
      endRemoveRows();
    }
    updateState(false);
  }
}

void MessageHistoryModel::reset() {
  beginResetModel();
  sigs.clear();
  if (auto dbc_msg = GetDBC()->msg(msg_id)) {
    sigs = dbc_msg->getSignals();
  }
  messages.clear();
  hex_colors = {};
  endResetModel();
  setFilter(0, "", nullptr);
}

QVariant MessageHistoryModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation == Qt::Horizontal) {
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
      if (section == 0) return "Time";
      if (isHexMode()) return "Data";

      QString name = sigs[section - 1]->name;
      QString unit = sigs[section - 1]->unit;
      return unit.isEmpty() ? name : QString("%1 (%2)").arg(name, unit);
    } else if (role == Qt::BackgroundRole && section > 0 && !isHexMode()) {
      // Alpha-blend the signal color with the background to ensure contrast
      QColor sigColor = sigs[section - 1]->color;
      sigColor.setAlpha(128);
      return QBrush(sigColor);
    }
  }
  return {};
}

void MessageHistoryModel::setHexMode(bool hex) {
  hex_mode = hex;
  reset();
}

void MessageHistoryModel::setFilter(int sig_idx, const QString &value, std::function<bool(double, double)> cmp) {
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

  if (is_paused && !clear) return;

  auto *stream = StreamManager::stream();
  uint64_t current_time = stream->toMonoTime(stream->snapshot(msg_id)->ts) + 1;
  uint64_t last_time = messages.empty() ? 0 : messages.front().mono_time;

  // Fetch new messages at the front (Live Tail)
  fetchData(messages.begin(), current_time, last_time);

  if (!is_paused && messages.size() > LIVE_VIEW_LIMIT) {
    beginRemoveRows({}, LIVE_VIEW_LIMIT, messages.size() - 1);
    messages.erase(messages.begin() + LIVE_VIEW_LIMIT, messages.end());
    endRemoveRows();
  }
}

bool MessageHistoryModel::canFetchMore(const QModelIndex& parent) const {
  // Strategy: Only allow fetching older history when paused to prevent list jumps
  if (!is_paused || messages.empty()) return false;

  const auto& events = StreamManager::stream()->events(msg_id);
  if (events.empty()) return false;

  return messages.back().mono_time > events.front()->mono_time;
}

void MessageHistoryModel::fetchMore(const QModelIndex &parent) {
  if (messages.empty()) return;
  // Fetch older data at the end (Infinite Scroll)
  fetchData(messages.end(), messages.back().mono_time, 0);
}

void MessageHistoryModel::fetchData(std::deque<Message>::iterator insert_pos, uint64_t from_time, uint64_t min_time) {
  const auto &events = StreamManager::stream()->events(msg_id);
  if (events.empty()) return;

  auto first = std::upper_bound(events.rbegin(), events.rend(), from_time, [](uint64_t ts, auto e) {
    return ts > e->mono_time;
  });

  std::vector<MessageHistoryModel::Message> msgs;
  std::vector<double> values(sigs.size());
  msgs.reserve(batch_size);
  for (; first != events.rend() && (*first)->mono_time > min_time; ++first) {
    const CanEvent *e = *first;
    for (int i = 0; i < sigs.size(); ++i) {
      sigs[i]->getValue(e->dat, e->size, &values[i]);
    }
    if (!filter_cmp || filter_cmp(values[filter_sig_idx], filter_value)) {
       msgs.emplace_back(Message{e->mono_time, values, {e->dat, e->dat + e->size}});
      if (msgs.size() >= batch_size && min_time == 0) {
        break;
      }
    }
  }

  if (!msgs.empty()) {
    if (isHexMode() && (min_time > 0 || messages.empty())) {
      const auto freq = StreamManager::stream()->snapshot(msg_id)->freq;
      for (auto &m : msgs) {
        hex_colors.update(msg_id, m.data.data(), m.data.size(), m.mono_time / (double)1e9, StreamManager::stream()->getSpeed(), freq);
        hex_colors.updateAllPatternColors(m.mono_time / (double)1e9);
        m.colors = hex_colors.colors;
      }
    }
    int pos = std::distance(messages.begin(), insert_pos);
    beginInsertRows({}, pos , pos + msgs.size() - 1);
    messages.insert(insert_pos, std::move_iterator(msgs.begin()), std::move_iterator(msgs.end()));
    endInsertRows();
  }
}
