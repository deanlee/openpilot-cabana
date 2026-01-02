#include "message_table.h"

#include "widgets/messageswidget.h"

QVariant MessageTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
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

QVariant MessageTableModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= items_.size()) return {};

  auto getFreq = [](float freq) {
    if (freq > 0) {
      return freq >= 0.95 ? QString::number(std::nearbyint(freq)) : QString::number(freq, 'f', 2);
    } else {
      return QStringLiteral("--");
    }
  };

  const static QString NA = QStringLiteral("N/A");
  const auto &item = items_[index.row()];
  if (role == Qt::DisplayRole) {
    switch (index.column()) {
      case Column::NAME: return item.name;
      case Column::SOURCE: return item.id.source != INVALID_SOURCE ? QString::number(item.id.source) : NA;
      case Column::ADDRESS: return toHexString(item.id.address);
      case Column::NODE: return item.node;
      case Column::FREQ: return item.id.source != INVALID_SOURCE ? getFreq(item.data->freq) : NA;
      case Column::COUNT: return item.id.source != INVALID_SOURCE ? QString::number(item.data->count) : NA;
      case Column::DATA: return item.id.source != INVALID_SOURCE ? "" : NA;
    }
  } else if (role == ColorsRole) {
    return QVariant::fromValue((void*)(&item.data->colors));
  } else if (role == BytesRole && index.column() == Column::DATA && item.id.source != INVALID_SOURCE) {
    return QVariant::fromValue((void*)(&item.data->dat));
  } else if (role == Qt::ToolTipRole && index.column() == Column::NAME) {
    auto msg = dbc()->msg(item.id);
    auto tooltip = item.name;
    if (msg && !msg->comment.isEmpty()) tooltip += "<br /><span style=\"color:gray;\">" + msg->comment + "</span>";
    return tooltip;
  }
  return {};
}

void MessageTableModel::setFilterStrings(const QMap<int, QString> &filters) {
  filters_ = filters;
  filterAndSort();
}

void MessageTableModel::showInactivemessages(bool show) {
  show_inactive_messages = show;
  filterAndSort();
}

void MessageTableModel::dbcModified() {
  dbc_messages_.clear();
  for (const auto &[_, m] : dbc()->getMessages(-1)) {
    dbc_messages_.insert(MessageId{INVALID_SOURCE, m.address});
  }
  filterAndSort();
}

void MessageTableModel::sortItems(std::vector<MessageTableModel::Item> &items) {
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

bool MessageTableModel::match(const MessageTableModel::Item &item) {
  if (filters_.isEmpty())
    return true;

  bool match = true;
  for (auto it = filters_.cbegin(); it != filters_.cend() && match; ++it) {
    const QString &txt = it.value();
    switch (it.key()) {
      case Column::NAME: {
        match = item.name.contains(txt, Qt::CaseInsensitive);
        if (!match) {
          const auto m = dbc()->msg(item.id);
          match = m && std::any_of(m->sigs.cbegin(), m->sigs.cend(),
                                   [&txt](const auto &s) { return s->name.contains(txt, Qt::CaseInsensitive); });
        }
        break;
      }
      case Column::SOURCE:
        match = parseRange(txt, item.id.source);
        break;
      case Column::ADDRESS:
        match = toHexString(item.id.address).contains(txt, Qt::CaseInsensitive);
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

bool MessageTableModel::filterAndSort() {
  // merge CAN and DBC messages
  std::vector<MessageId> all_messages;
  all_messages.reserve(can->snapshots().size() + dbc_messages_.size());
  auto dbc_msgs = dbc_messages_;
  for (const auto &[id, m] : can->snapshots()) {
    all_messages.push_back(id);
    dbc_msgs.erase(MessageId{INVALID_SOURCE, id.address});
  }
  all_messages.insert(all_messages.end(), dbc_msgs.begin(), dbc_msgs.end());

  // filter and sort
  std::vector<Item> items;
  items.reserve(all_messages.size());
  for (const auto &id : all_messages) {
    if (show_inactive_messages || can->isMessageActive(id)) {
      auto msg = dbc()->msg(id);
      Item item = {
          .id = id,
          .name = msg ? msg->name : UNTITLED,
          .node = msg ? msg->transmitter : QString(),
          .data = can->snapshot(id),
      };
      if (match(item))
        items.emplace_back(item);
    }
  }
  sortItems(items);

  if (items_ != items) {
    beginResetModel();
    items_ = std::move(items);
    endResetModel();
    return true;
  }
  return false;
}

void MessageTableModel::onSnapshotsUpdated(const std::set<MessageId> *ids, bool needs_rebuild) {
  if (needs_rebuild || ((filters_.count(Column::FREQ) || filters_.count(Column::COUNT) || filters_.count(Column::DATA)) &&
                      ++sort_threshold_ == settings.fps)) {
    sort_threshold_ = 0;
    if (filterAndSort()) return;
  }

  // Update viewport
  MessagesWidget *widget = qobject_cast<MessagesWidget*>(parent());
  if (widget && widget->view) {
    widget->view->viewport()->update();
  }
}

void MessageTableModel::sort(int column, Qt::SortOrder order) {
  if (column != Column::DATA) {
    sort_column = column;
    sort_order = order;
    filterAndSort();
  }
}
