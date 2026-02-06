#include "message_model.h"

#include <QApplication>
#include <QPalette>
#include <cmath>
#include <unordered_set>

#include "message_delegate.h"
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

MessageModel::MessageModel(QObject *parent) : QAbstractTableModel(parent) {}

QVariant MessageModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};

  static const QVector<QString> headers = {"Name", "Bus", "ID", "Node", "Freq", "Count", "Bytes"};
  return (section >= 0 && section < headers.size()) ? headers[section] : QVariant();
}

QVariant MessageModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= items_.size()) return {};

  const auto &item = items_[index.row()];
  if (role == Qt::DisplayRole) {
    switch (index.column()) {
      case Column::NAME: return item.name;
      case Column::SOURCE: return item.id.source != INVALID_SOURCE ? QString::number(item.id.source) : NA;
      case Column::ADDRESS: return item.address_hex;
      case Column::NODE: return item.node;
      case Column::FREQ: return formatFreq(item);
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

  if (role == ColumnTypeRole::MsgActiveRole) {
    return bool(item.data && item.data->is_active);
  }

  if (role == ColumnTypeRole::IsHexColumn) {
    return index.column() == Column::DATA;
  }

  return {};
}

void MessageModel::setInactiveMessagesVisible(bool show) {
  show_inactive_ = show;
  rebuild();
}

void MessageModel::sortItems(std::vector<MessageModel::Item>& items) const {
  if (items.empty()) return;

  auto comp = [order = sort_order](const auto& a, const auto& b) {
    return order == Qt::AscendingOrder ? a < b : a > b;
  };

  switch (sort_column) {
    case Column::NAME:
      std::ranges::sort(items, comp, [](const Item& i) { return std::tie(i.name, i.id); });
      break;
    case Column::SOURCE:
      std::ranges::sort(items, comp, [](const Item& i) { return std::tie(i.id.source, i.id.address); });
      break;
    case Column::ADDRESS: std::ranges::sort(items, comp, [](const Item& i) { return std::tie(i.id.address, i.id.source); });
      break;
    case Column::NODE:
      std::ranges::sort(items, comp, [](const Item& i) { return std::tie(i.node, i.id);});
      break;
    case Column::FREQ:
      std::ranges::sort(items, comp, [](const Item& i) {
        double freq = i.data ? i.data->freq : -1.0;
        return std::pair(freq, i.id);
      });
      break;
    case Column::COUNT:
      std::ranges::sort(items, comp, [](const Item& i) {
        uint32_t count = i.data ? i.data->count : 0;
        return std::pair(count, i.id);
      });
      break;
    default:
      break;
  }
}

QString MessageModel::formatFreq(const Item &item) const {
  if (!item.data) return NA;

  const float f = item.data->freq;
  if (std::abs(item.last_freq - f) > 0.01f) {
    item.last_freq = f;
    item.freq_str = (f <= 0.0f) ? DASH :
                    (f >= 0.95f) ? QString::number(std::nearbyint(f)) :
                                   QString::number(f, 'f', 2);
  }
  return item.freq_str;
}

void MessageModel::setFilterStrings(const QMap<int, QString> &filters) {
  filters_ = filters;
  filter_ranges_.clear();

  for (auto it = filters.cbegin(); it != filters.cend(); ++it) {
    int col = it.key();
    // Only pre-parse numeric/range columns
    if (col == Column::SOURCE || col == Column::ADDRESS || col == Column::FREQ || col == Column::COUNT) {
      if (auto range = parseFilter(it.value(), col == Column::ADDRESS ? 16 : 10)) {
        filter_ranges_[col] = *range;
      }
    }
  }

  rebuild();
}

std::optional<MessageModel::FilterRange> MessageModel::parseFilter(QString filter, int base) {
  FilterRange r;
  filter = filter.simplified().replace(" ", "");
  if (filter.isEmpty()) return std::nullopt;

  auto parse = [&](const QString& s, bool* ok) {
    return (base == 16) ? (double)s.toUInt(ok, 16) : s.toDouble(ok);
  };

  QStringList parts = filter.split('-');
  bool ok = true;

  if (parts.size() == 1) {
    r.min = r.max = parse(parts[0], &ok);
    r.is_exact = true;
  } else if (parts.size() == 2) {
    if (!parts[0].isEmpty()) r.min = parse(parts[0], &ok);
    if (!parts[1].isEmpty()) r.max = parse(parts[1], &ok);
  }

  return ok ? std::optional<FilterRange>(r) : std::nullopt;
}

bool MessageModel::match(const MessageModel::Item &item) const {
  for (auto it = filters_.cbegin(); it != filters_.cend(); ++it) {
    const int col = it.key();
    const QString &txt = it.value();

    switch (col) {
      case Column::NAME:
        if (item.name.contains(txt, Qt::CaseInsensitive)) continue;
        if (auto m = GetDBC()->msg(item.id)) {
          if (std::any_of(m->sigs.begin(), m->sigs.end(), [&](auto& s){ return s->name.contains(txt, Qt::CaseInsensitive); })) continue;
        }
        return false;

      case Column::NODE:
        if (!item.node.contains(txt, Qt::CaseInsensitive)) return false;
        continue;

      case Column::DATA:
        if (!item.data || !utils::toHex(item.data->data.data(), item.data->size).contains(txt, Qt::CaseInsensitive)) return false;
        continue;

      case Column::ADDRESS:
        if (item.address_hex.contains(txt, Qt::CaseInsensitive)) continue; // Fallthrough to range check
        [[fallthrough]];

      default: { // SOURCE, FREQ, COUNT, and ADDRESS range
        auto it_range = filter_ranges_.find(col);
        if (it_range == filter_ranges_.end()) return false;

        const auto &r = it_range.value();

        double val = (col == Column::SOURCE) ? item.id.source :
                     (col == Column::ADDRESS) ? item.id.address :
                     (col == Column::FREQ) ? (item.data ? item.data->freq : -1) :
                     (item.data ? (double)item.data->count : -1);

        if (r.is_exact ? (std::abs(val - r.min) > 0.001) : (val < r.min || val > r.max)) return false;
        break;
      }
    }
  }
  return true;
}

std::vector<MessageModel::Item> MessageModel::fetchItems() const {
  const auto& snapshots = StreamManager::stream()->snapshots();
  auto* dbc = GetDBC();
  const auto& dbc_messages = dbc->getMessages();

  std::vector<Item> new_items;
  new_items.reserve(snapshots.size() + dbc_messages.size());

  std::unordered_set<uint32_t> snapshot_addrs;
  snapshot_addrs.reserve(snapshots.size());

  auto processItem = [&](const MessageId& id, const dbc::Msg* msg, const MessageSnapshot* data) {
    QString addr_hex = getHexCached(id.address);
    Item item = {
        .id = id,
        .name = msg ? msg->name : QString("[%1]").arg(addr_hex),
        .node = (msg && !msg->transmitter.isEmpty()) ? msg->transmitter : QStringLiteral("—"),
        .data = data,
        .address_hex = addr_hex,
    };

    if (match(item)) {
      new_items.push_back(std::move(item));
    }
  };

  // Process Live Snapshots
  for (const auto& [id, data] : snapshots) {
    snapshot_addrs.insert(id.address);
    if (show_inactive_ || (data && data->is_active)) {
      processItem(id, dbc->msg(id), data.get());
    }
  }

  // Process DBC placeholders
  if (show_inactive_) {
    for (const auto& [address, msg] : dbc_messages) {
      if (snapshot_addrs.find(address) == snapshot_addrs.end()) {
        processItem({INVALID_SOURCE, address}, &msg, nullptr);
      }
    }
  }

  sortItems(new_items);
  return new_items;
}

void MessageModel::rebuild() {
  std::vector<Item> new_items = fetchItems();

  dbc_msg_count_ = 0;
  signal_count_ = 0;
  auto *dbc = GetDBC();
  for (const auto& item : new_items) {
    if (auto m = dbc->msg(item.id)) {
      dbc_msg_count_++;
      signal_count_ += m->sigs.size();
    }
  }

  // Check if the IDs or count changed (affects UI structure)
  bool structureChanged = (items_.size() != new_items.size()) ||
                          !std::equal(items_.begin(), items_.end(), new_items.begin(),
                                      [](const Item& a, const Item& b) { return a.id == b.id; });
  if (structureChanged) {
    // IDs changed or items added/removed: Reset is necessary
    beginResetModel();
    items_ = std::move(new_items);
    endResetModel();
  } else {
    // Structure is identical: Just update the data pointers and repaint
    items_ = std::move(new_items);
    emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
    emit layoutChanged();
  }
}

void MessageModel::onSnapshotsUpdated(const std::set<MessageId> *ids, bool needs_rebuild) {
  if (needs_rebuild || ((filters_.count(Column::FREQ) || filters_.count(Column::COUNT) || filters_.count(Column::DATA)) &&
                      ++sort_threshold_ == settings.fps)) {
    sort_threshold_ = 0;
    rebuild();
    return;
  }

  for (int i = 0; i < items_.size(); ++i) {
    if (!ids || ids->find(items_[i].id) != ids->end()) {
      for (int c = MessageModel::Column::FREQ; c <= MessageModel::Column::DATA; ++c) {
        emit dataChanged(index(i, c), index(i, c));
      }
    }
  }
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
