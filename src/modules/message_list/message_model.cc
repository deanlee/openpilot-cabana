#include "message_model.h"

#include <QApplication>
#include <QPalette>
#include <cmath>
#include <unordered_set>

#include "modules/settings/settings.h"
#include "modules/system/stream_manager.h"

static const QString NA = QStringLiteral("N/A");
static const QString DASH = QString::fromUtf8("\xE2\x80\x94");

static const QString getHexCached(uint32_t addr) {
  static QHash<uint32_t, QString> cache;
  auto it = cache.find(addr);
  if (it == cache.end()) {
    it = cache.insert(addr, "0x" + QString::number(addr, 16).toUpper().rightJustified(2, '0'));
  }
  return *it;
}

MessageModel::MessageModel(QObject *parent) : QAbstractTableModel(parent) {
  disabled_color_ = QApplication::palette().color(QPalette::Disabled, QPalette::Text);
}

QVariant MessageModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};

  static const QVector<QString> headers = {"Name", "Bus", "ID", "Node", "Freq", "Count", "Bytes"};
  return (section >= 0 && section < headers.size()) ? headers[section] : QVariant();
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
      case Column::FREQ: return item.data ? getFreq(item.data->freq) : NA;
      case Column::COUNT: return item.data ? QString::number(item.data->count) : NA;
      case Column::DATA: return item.data ? "" : NA;
      default: return {};
    }
  }

  if (role == Qt::ToolTipRole && index.column() == Column::NAME) {
    auto msg = GetDBC()->msg(item.id);
    if (msg && !msg->comment.isEmpty()) {
      return QString("%1<br/><span style=\"color:gray;\">%2</span>").arg(item.name, msg->comment);
    }
    return item.name;
  }

  if (role == Qt::ForegroundRole) {
    return (item.data && item.data->is_active) ? QVariant() : disabled_color_;
  }

  return {};
}

void MessageModel::setFilterStrings(const QMap<int, QString> &filters) {
  filters_ = filters;
  rebuild();
}

void MessageModel::setInactiveMessagesVisible(bool show) {
  show_inactive_ = show;
  rebuild();
}

void MessageModel::sortItems(std::vector<MessageModel::Item> &items) {
  if (items.empty()) return;

  auto do_sort = [this, &items](auto compare) {
    if (sort_order == Qt::AscendingOrder) {
      std::sort(items.begin(), items.end(), compare);
    } else {
      std::sort(items.begin(), items.end(), [&compare](const Item &l, const Item &r) { return compare(r, l);});
    }
  };

  switch (sort_column) {
    case Column::NAME: do_sort([](const Item &l, const Item &r) { return l.name < r.name || (l.name == r.name && l.id < r.id); }); break;
    case Column::SOURCE: do_sort([](const Item &l, const Item &r) { return l.id.source < r.id.source || (l.id.source == r.id.source && l.id.address < r.id.address); }); break;
    case Column::ADDRESS: do_sort([](const Item &l, const Item &r) { return l.id.address < r.id.address || (l.id.address == r.id.address && l.id.source < r.id.source); }); break;
    case Column::NODE: do_sort([](const Item &l, const Item &r) { return (l.node < r.node) || (l.node == r.node && l.id < r.id); }); break;
    case Column::FREQ: do_sort([](const Item &l, const Item &r) {
      double l_freq = l.data ? l.data->freq : -1.0;
      double r_freq = r.data ? r.data->freq : -1.0;
      return l_freq != r_freq ? l_freq < r_freq : l.id < r.id; }); break;
    case Column::COUNT: do_sort([](const Item &l, const Item &r) {
      uint32_t l_count = l.data ? l.data->count : 0;
      uint32_t r_count = r.data ? r.data->count : 0;
      return l_count != r_count ? l_count < r_count : l.id < r.id; }); break;
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
        match = item.data ? parseRange(txt, item.data->freq) : false;
        break;
      case Column::COUNT:
        match = item.data ? parseRange(txt, item.data->count) : false;
        break;
      case Column::DATA:
        match = item.data ? utils::toHex(item.data->dat).contains(txt, Qt::CaseInsensitive) : false;
        break;
    }
  }
  return match;
}

bool MessageModel::rebuild() {
  const auto& snapshots = StreamManager::stream()->snapshots();
  const auto &dbc_messages = GetDBC()->getMessages();

  std::vector<Item> new_items;
  new_items.reserve(snapshots.size() + dbc_messages.size());

  // Set to track addresses already handled by snapshots
  std::unordered_set<uint32_t> snapshot_addrs;
  snapshot_addrs.reserve(snapshots.size());

  // Helper: Builds item, matches against filters, and moves to vector
  auto processItem = [&](const MessageId& id, const dbc::Msg *msg, const MessageState* data) {
    Item item = {
        .id = id,
        .name = msg ? msg->name : DASH,
        .node = msg ? msg->transmitter : QString(),
        .data = data,
        .address_hex = getHexCached(id.address),
    };

    if (match(item)) {
      new_items.push_back(std::move(item));
    }
  };

  // Process Live Snapshots
  auto *dbc = GetDBC();
  for (const auto& [id, data] : snapshots) {
    snapshot_addrs.insert(id.address);
    if (show_inactive_ || (data && data->is_active)) {
      processItem(id, dbc->msg(id), data.get());
    }
  }

  // Process DBC placeholders (only if address not on live bus)
  if (show_inactive_) {
    for (const auto& [address, msg] : dbc_messages) {
      if (snapshot_addrs.find(address) == snapshot_addrs.end()) {
        processItem(MessageId{INVALID_SOURCE, address}, &msg, nullptr);
      }
    }
  }

  sortItems(new_items);

  bool structureChanged = (items_.size() != new_items.size()) ||
                          !std::equal(items_.begin(), items_.end(), new_items.begin(),
                                      [](const auto& a, const auto& b) { return a.id == b.id; });
  if (structureChanged) {
    // IDs changed or items added/removed: Reset is necessary
    beginResetModel();
    items_ = std::move(new_items);
    endResetModel();
  } else {
    // Structure is identical: Just update the data pointers and repaint
    items_ = std::move(new_items);
    emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
  }
  return false;
}

void MessageModel::onSnapshotsUpdated(const std::set<MessageId> *ids, bool needs_rebuild) {
  if (needs_rebuild || ((filters_.count(Column::FREQ) || filters_.count(Column::COUNT) || filters_.count(Column::DATA)) &&
                      ++sort_threshold_ == settings.fps)) {
    sort_threshold_ = 0;
    if (rebuild()) return;
  }

  emit uiUpdateRequired();
}

void MessageModel::sort(int column, Qt::SortOrder order) {
  if (column != Column::DATA) {
    sort_column = column;
    sort_order = order;
    emit layoutAboutToBeChanged();
    sortItems(items_);
    emit layoutChanged();
  }
}
