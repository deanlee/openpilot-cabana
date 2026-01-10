#include "message_model.h"

#include <QApplication>
#include <cmath>
#include <unordered_set>

#include "message_list.h"
#include "modules/settings/settings.h"
#include "modules/system/stream_manager.h"

static const QString NA = QStringLiteral("N/A");
static const QString DASH = QStringLiteral("--");

inline QString toHexString(int value) {
  return "0x" + QString::number(value, 16).toUpper().rightJustified(2, '0');
}

QVariant MessageModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
    switch (section) {
      case Column::NAME: return tr("Name");
      case Column::SOURCE: return tr("Bus");
      case Column::ADDRESS: return tr("ID");
      case Column::NODE: return tr("Node");
      case Column::FREQ: return tr("Freq");
      case Column::COUNT: return tr("Count");
      case Column::DATA: return tr("Bytes");
    }
  }
  return {};
}

QVariant MessageModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= items_.size()) return {};

  auto getFreq = [](float freq) {
    if (freq > 0) {
      return freq >= 0.95 ? QString::number(std::nearbyint(freq)) : QString::number(freq, 'f', 2);
    } else {
      return DASH;
    }
  };

  const auto &item = items_[index.row()];
  if (role == Qt::DisplayRole) {
    switch (index.column()) {
      case Column::NAME: return item.name;
      case Column::SOURCE: return item.id.source != INVALID_SOURCE ? QString::number(item.id.source) : NA;
      case Column::ADDRESS: return item.address_hex;
      case Column::NODE: return item.node;
      case Column::FREQ: return item.id.source != INVALID_SOURCE ? getFreq(item.data->freq) : NA;
      case Column::COUNT: return item.id.source != INVALID_SOURCE ? QString::number(item.data->count) : NA;
      case Column::DATA: return item.id.source != INVALID_SOURCE ? "" : NA;
    }
  } else if (role == ColorsRole) {
    return QVariant::fromValue((void*)(&(item.data->colors)));
  } else if (role == BytesRole && index.column() == Column::DATA && item.id.source != INVALID_SOURCE) {
    return QVariant::fromValue((void*)(&item.data->dat));
  } else if (role == Qt::ToolTipRole && index.column() == Column::NAME) {
    auto msg = GetDBC()->msg(item.id);
    auto tooltip = item.name;
    if (msg && !msg->comment.isEmpty()) tooltip += "<br /><span style=\"color:gray;\">" + msg->comment + "</span>";
    return tooltip;
  } else if (role == Qt::ForegroundRole) {
    if (!item.data->is_active) {
      return QApplication::palette().color(QPalette::Disabled, QPalette::Text);
    }
  }
  return {};
}

void MessageModel::setFilterStrings(const QMap<int, QString> &filters) {
  filters_ = filters;
  filterAndSort();
}

void MessageModel::showInactivemessages(bool show) {
  show_inactive_messages = show;
  filterAndSort();
}

void MessageModel::dbcModified() {
  dbc_messages_.clear();
  for (const auto &[_, m] : GetDBC()->getMessages(-1)) {
    dbc_messages_.insert(MessageId{INVALID_SOURCE, m.address});
  }
  filterAndSort();
}

void MessageModel::sortItems(std::vector<MessageModel::Item> &items) {
  auto do_sort = [this, &items](auto compare) {
    if (sort_order == Qt::DescendingOrder)
      std::stable_sort(items.rbegin(), items.rend(), compare);
    else
      std::stable_sort(items.begin(), items.end(), compare);
  };

  switch (sort_column) {
    case Column::NAME: do_sort([](auto &l, auto &r){ return std::tie(l.name, l.id) < std::tie(r.name, r.id); }); break;
    case Column::SOURCE: do_sort([](auto &l, auto &r){ return std::tie(l.id.source, l.id.address) < std::tie(r.id.source, r.id.address); }); break;
    case Column::ADDRESS: do_sort([](auto &l, auto &r){ return std::tie(l.id.address, l.id.source) < std::tie(r.id.address, r.id.source); }); break;
    case Column::NODE: do_sort([](auto &l, auto &r){ return std::tie(l.node, l.id) < std::tie(r.node, r.id); }); break;
    case Column::FREQ: do_sort([](auto &l, auto &r){ return std::tie(l.data->freq, l.id) < std::tie(r.data->freq, r.id); }); break;
    case Column::COUNT: do_sort([](auto &l, auto &r){ return std::tie(l.data->count, l.id) < std::tie(r.data->count, r.id); }); break;
    default: break; // Default case to suppress compiler warning
  }
}

static bool parseRange(const QString &filter, uint32_t value, int base = 10) {
  // Parse out filter string into a range (e.g. "1" -> {1, 1}, "1-3" -> {1, 3}, "1-" -> {1, inf})
  unsigned int min = std::numeric_limits<unsigned int>::min();
  unsigned int max = std::numeric_limits<unsigned int>::max();
  auto s = filter.split('-');
  bool ok = s.size() >= 1 && s.size() <= 2;
  if (ok && !s[0].isEmpty()) min = s[0].toUInt(&ok, base);
  if (ok && s.size() == 1) {
    max = min;
  } else if (ok && s.size() == 2 && !s[1].isEmpty()) {
    max = s[1].toUInt(&ok, base);
  }
  return ok && value >= min && value <= max;
}

bool MessageModel::match(const MessageModel::Item &item) {
  if (filters_.isEmpty())
    return true;

  bool match = true;
  for (auto it = filters_.cbegin(); it != filters_.cend() && match; ++it) {
    const QString &txt = it.value();
    switch (it.key()) {
      case Column::NAME: {
        match = item.name.contains(txt, Qt::CaseInsensitive);
        if (!match) {
          const auto m = GetDBC()->msg(item.id);
          match = m && std::any_of(m->sigs.cbegin(), m->sigs.cend(),
                                   [&txt](const auto &s) { return s->name.contains(txt, Qt::CaseInsensitive); });
        }
        break;
      }
      case Column::SOURCE:
        match = parseRange(txt, item.id.source);
        break;
      case Column::ADDRESS:
        match = item.address_hex.contains(txt, Qt::CaseInsensitive);
        match = match || parseRange(txt, item.id.address, 16);
        break;
      case Column::NODE:
        match = item.node.contains(txt, Qt::CaseInsensitive);
        break;
      case Column::FREQ:
        match = parseRange(txt, item.data->freq);
        break;
      case Column::COUNT:
        match = parseRange(txt, item.data->count);
        break;
      case Column::DATA:
        match = utils::toHex(item.data->dat).contains(txt, Qt::CaseInsensitive);
        break;
    }
  }
  return match;
}

bool MessageModel::filterAndSort() {
  auto *can = StreamManager::stream();
  const auto& snapshots = can->snapshots();
  auto* dbc = GetDBC();

  std::vector<Item> new_items;
  new_items.reserve(snapshots.size() + dbc_messages_.size());

  // Set to track addresses already handled by snapshots
  std::unordered_set<uint32_t> snapshot_addrs;
  snapshot_addrs.reserve(snapshots.size());

  // Helper: Builds item, matches against filters, and moves to vector
  auto processItem = [&](const MessageId& id, const MessageState* data) {
    auto msg = dbc->msg(id);
    Item item = {
        .id = id,
        .name = msg ? msg->name : UNTITLED,
        .node = msg ? msg->transmitter : QString(),
        .data = data,
        .address_hex = toHexString(id.address),
    };

    if (match(item)) {
      new_items.push_back(std::move(item));
    }
  };

  // Process Live Snapshots
  for (const auto& [id, data] : snapshots) {
    snapshot_addrs.insert(id.address);
    if (show_inactive_messages || (data && data->is_active)) {
      processItem(id, data.get());
    }
  }

  // Process DBC placeholders (only if address not on live bus)
  if (show_inactive_messages) {
    for (const auto& id : dbc_messages_) {
      if (snapshot_addrs.find(id.address) == snapshot_addrs.end()) {
        processItem(id, can->snapshot(id));
      }
    }
  }

  sortItems(new_items);

  if (items_ != new_items) {
    beginResetModel();
    items_ = std::move(new_items);
    endResetModel();
    return true;
  }
  return false;
}

void MessageModel::onSnapshotsUpdated(const std::set<MessageId> *ids, bool needs_rebuild) {
  if (needs_rebuild || ((filters_.count(Column::FREQ) || filters_.count(Column::COUNT) || filters_.count(Column::DATA)) &&
                      ++sort_threshold_ == settings.fps)) {
    sort_threshold_ = 0;
    if (filterAndSort()) return;
  }

  // Update viewport
  MessageList *widget = qobject_cast<MessageList*>(parent());
  if (widget && widget->view) {
    widget->view->viewport()->update();
  }
}

void MessageModel::sort(int column, Qt::SortOrder order) {
  if (column != Column::DATA) {
    sort_column = column;
    sort_order = order;
    filterAndSort();
  }
}
