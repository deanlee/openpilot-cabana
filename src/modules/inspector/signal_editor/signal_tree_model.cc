#include "signal_tree_model.h"

#include <QMessageBox>

#include "core/commands/commands.h"
#include "core/dbc/dbc_manager.h"
#include "modules/inspector/binary/binary_model.h"

static const QStringList SIGNAL_PROPERTY_LABELS = {
    "Name", "Size", "Receiver Nodes", "Little Endian", "Signed", "Offset", "Factor", 
    "Type", "Multiplex Value", "Extra Info", "Unit", "Comment", "Min", "Max", "Value Table"
};

QString signalTypeToString(dbc::Signal::Type type) {
  if (type == dbc::Signal::Type::Multiplexor) return "Multiplexor Signal";
  else if (type == dbc::Signal::Type::Multiplexed) return "Multiplexed Signal";
  else return "Normal Signal";
}

SignalTreeModel::SignalTreeModel(QObject *parent) : root(new Item), QAbstractItemModel(parent) {
  connect(GetDBC(), &dbc::Manager::DBCFileChanged, this, &SignalTreeModel::refresh);
  connect(GetDBC(), &dbc::Manager::msgUpdated, this, &SignalTreeModel::handleMsgChanged);
  connect(GetDBC(), &dbc::Manager::msgRemoved, this, &SignalTreeModel::handleMsgChanged);
  connect(GetDBC(), &dbc::Manager::signalAdded, this, &SignalTreeModel::handleSignalAdded);
  connect(GetDBC(), &dbc::Manager::signalUpdated, this, &SignalTreeModel::handleSignalUpdated);
  connect(GetDBC(), &dbc::Manager::signalRemoved, this, &SignalTreeModel::handleSignalRemoved);
}

void SignalTreeModel::insertItem(SignalTreeModel::Item *root_item, int pos, const dbc::Signal *sig) {
  Item *sig_item = new Item{.sig = sig, .parent = root_item, .title = sig->name, .type = Item::Sig};
  root_item->children.insert(pos, sig_item);
}

void SignalTreeModel::setMessage(const MessageId &id) {
  msg_id = id;
  filter_str = "";
  refresh();
}

void SignalTreeModel::updateChartedSignals(const QMap<MessageId, QSet<const dbc::Signal*>> &opened) {
  charted_signals_ = opened;
  if (rowCount() > 0) {
    emit dataChanged(index(0, 0), index(rowCount() - 1, 1), {IsChartedRole});
  }
}

void SignalTreeModel::setFilter(const QString &txt) {
  filter_str = txt;
  refresh();
}

void SignalTreeModel::refresh() {
  beginResetModel();
  root.reset(new SignalTreeModel::Item);
  if (auto msg = GetDBC()->msg(msg_id)) {
    auto sigs = msg->getSignals();
    root->children.reserve(sigs.size());  // Pre-allocate memory
    for (auto s : sigs) {
      if (filter_str.isEmpty() || s->name.contains(filter_str, Qt::CaseInsensitive)) {
        insertItem(root.get(), root->children.size(), s);
      }
    }
  }
  endResetModel();
}

SignalTreeModel::Item *SignalTreeModel::getItem(const QModelIndex &index) const {
  auto item = index.isValid() ? (SignalTreeModel::Item *)index.internalPointer() : nullptr;
  return item ? item : root.get();
}

int SignalTreeModel::rowCount(const QModelIndex &parent) const {
  if (parent.column() > 0) return 0;
  return getItem(parent)->children.size();
}

bool SignalTreeModel::hasChildren(const QModelIndex &parent) const {
  if (!parent.isValid()) return true;
  Item *item = getItem(parent);
  return item->type == Item::Sig || item->type == Item::ExtraInfo;
}

bool SignalTreeModel::canFetchMore(const QModelIndex &parent) const {
  if (!parent.isValid()) return false;
  Item *item = getItem(parent);

  return (item->type == Item::Sig || item->type == Item::ExtraInfo) && item->children.isEmpty();
}

void SignalTreeModel::fetchMore(const QModelIndex& parent) {
  if (!parent.isValid()) return;
  Item* item = getItem(parent);

  QList<Item::Type> types;
  if (item->type == Item::Sig) {
    types = {Item::Name, Item::Size, Item::Node, Item::Endian, Item::Signed,
             Item::Offset, Item::Factor, Item::SignalType, Item::MultiplexValue, Item::ExtraInfo};
  } else if (item->type == Item::ExtraInfo) {
    types = {Item::Unit, Item::Comment, Item::Min, Item::Max, Item::ValueTable};
  }

  if (types.isEmpty()) return;

  // Notify the View that we are adding rows to this specific parent
  beginInsertRows(parent, 0, types.size() - 1);
  for (auto t : types) {
    QString label = SIGNAL_PROPERTY_LABELS[t - Item::Name];
    item->children.push_back(new Item{.type = t, .parent = item, .sig = item->sig, .title = label});
  }
  endInsertRows();
}

Qt::ItemFlags SignalTreeModel::flags(const QModelIndex& index) const {
  if (!index.isValid()) return Qt::NoItemFlags;

  const Item* item = getItem(index);
  Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;

  // Only the second column for non-parent nodes is interactive
  if (index.column() == 1 && item->type != Item::Sig && item->type != Item::ExtraInfo) {
    if (item->type == Item::Endian || item->type == Item::Signed) {
      f |= Qt::ItemIsUserCheckable;
    } else {
      f |= Qt::ItemIsEditable;
    }
  }

  // Business logic: disable multiplex settings if the signal isn't multiplexed
  if (item->type == Item::MultiplexValue && item->sig->type != dbc::Signal::Type::Multiplexed) {
    f &= ~Qt::ItemIsEnabled;
  }

  return f;
}

int SignalTreeModel::signalRow(const dbc::Signal *sig) const {
  for (int i = 0; i < root->children.size(); ++i) {
    if (root->children[i]->sig == sig) return i;
  }
  return -1;
}

QModelIndex SignalTreeModel::index(int row, int column, const QModelIndex &parent) const {
  if (parent.isValid() && parent.column() != 0) return {};

  auto parent_item = getItem(parent);
  if (parent_item && row < parent_item->children.size()) {
    return createIndex(row, column, parent_item->children[row]);
  }
  return {};
}

QModelIndex SignalTreeModel::parent(const QModelIndex &index) const {
  if (!index.isValid()) return {};
  Item *parent_item = getItem(index)->parent;
  return !parent_item || parent_item == root.get() ? QModelIndex() : createIndex(parent_item->row(), 0, parent_item);
}

QVariant SignalTreeModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid()) return {};

  const Item *item = getItem(index);
  if (role == Qt::DisplayRole || role == Qt::EditRole) {
    if (index.column() == 0) {
      return item->type == Item::Sig ? item->sig->name : item->title;
    }

    switch (item->type) {
      case Item::Sig: return item->sig_val;
      case Item::Name: return item->sig->name;
      case Item::Size: return item->sig->size;
      case Item::Node: return item->sig->receiver_name;
      case Item::SignalType: return signalTypeToString(item->sig->type);
      case Item::MultiplexValue: return item->sig->multiplex_value;
      case Item::Offset: return utils::doubleToString(item->sig->offset);
      case Item::Factor: return utils::doubleToString(item->sig->factor);
      case Item::Unit: return item->sig->unit;
      case Item::Comment: return item->sig->comment;
      case Item::Min: return utils::doubleToString(item->sig->min);
      case Item::Max: return utils::doubleToString(item->sig->max);
      case Item::ValueTable: {
        QStringList value_table;
        for (auto &[val, desc] : item->sig->value_table) {
          value_table << QString("%1 \"%2\"").arg(val).arg(desc);
        }
        return value_table.join(" ");
      }
      default: break;
    }
    return {};
  }

  if (role == Qt::CheckStateRole && index.column() == 1) {
    if (item->type == Item::Endian) return item->sig->is_little_endian ? Qt::Checked : Qt::Unchecked;
    if (item->type == Item::Signed) return item->sig->is_signed ? Qt::Checked : Qt::Unchecked;
  } else if (role == Qt::ToolTipRole && item->type == Item::Sig) {
    return (index.column() == 0) ? signalToolTip(item->sig) : QString();
  } else if (role == IsChartedRole && item->type == Item::Sig) {
    auto it = charted_signals_.find(msg_id);
    return (it != charted_signals_.end()) && it.value().contains(item->sig);
  }
  return {};
}

bool SignalTreeModel::setData(const QModelIndex &index, const QVariant &value, int role) {
  if (role != Qt::EditRole && role != Qt::CheckStateRole) return false;

  Item *item = getItem(index);
  dbc::Signal s = *item->sig;
  switch (item->type) {
    case Item::Name: s.name = value.toString(); break;
    case Item::Size: s.size = value.toInt(); break;
    case Item::Node: s.receiver_name = value.toString().trimmed(); break;
    case Item::SignalType: s.type = (dbc::Signal::Type)value.toInt(); break;
    case Item::MultiplexValue: s.multiplex_value = value.toInt(); break;
    case Item::Endian: s.is_little_endian = value.toBool(); break;
    case Item::Signed: s.is_signed = value.toBool(); break;
    case Item::Offset: s.offset = value.toDouble(); break;
    case Item::Factor: s.factor = value.toDouble(); break;
    case Item::Unit: s.unit = value.toString(); break;
    case Item::Comment: s.comment = value.toString(); break;
    case Item::Min: s.min = value.toDouble(); break;
    case Item::Max: s.max = value.toDouble(); break;
    case Item::ValueTable: s.value_table = value.value<ValueTable>(); break;
    default: return false;
  }
  bool ret = saveSignal(item->sig, s);
  emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, Qt::CheckStateRole});
  return ret;
}

bool SignalTreeModel::saveSignal(const dbc::Signal *origin_s, dbc::Signal &s) {
  auto msg = GetDBC()->msg(msg_id);
  if (s.name != origin_s->name && msg->sig(s.name) != nullptr) {
    QString text = tr("There is already a signal with the same name '%1'").arg(s.name);
    QMessageBox::warning(nullptr, tr("Failed to save signal"), text);
    return false;
  }

  if (s.is_little_endian != origin_s->is_little_endian) {
    s.start_bit = flipBitPos(s.start_bit);
  }
  UndoStack::push(new EditSignalCommand(msg_id, origin_s, s));
  return true;
}

void SignalTreeModel::handleMsgChanged(MessageId id) {
  if (id.address == msg_id.address) {
    refresh();
  }
}

void SignalTreeModel::handleSignalAdded(MessageId id, const dbc::Signal *sig) {
  if (id == msg_id) {
    if (filter_str.isEmpty()) {
      int i = GetDBC()->msg(msg_id)->indexOf(sig);
      beginInsertRows({}, i, i);
      insertItem(root.get(), i, sig);
      endInsertRows();
    } else if (sig->name.contains(filter_str, Qt::CaseInsensitive)) {
      refresh();
    }
  }
}

void SignalTreeModel::handleSignalUpdated(const dbc::Signal *sig) {
  if (int row = signalRow(sig); row != -1) {
    emit dataChanged(index(row, 0), index(row, 1), {Qt::DisplayRole, Qt::EditRole, Qt::CheckStateRole});

    if (filter_str.isEmpty()) {
      // move row when the order changes.
      int to = GetDBC()->msg(msg_id)->indexOf(sig);
      if (to != row) {
        beginMoveRows({}, row, row, {}, to > row ? to + 1 : to);
        root->children.move(row, to);
        endMoveRows();
      }
    }
  }
}

void SignalTreeModel::handleSignalRemoved(const dbc::Signal *sig) {
  if (int row = signalRow(sig); row != -1) {
    beginRemoveRows({}, row, row);
    delete root->children.takeAt(row);
    endRemoveRows();
  }
}
