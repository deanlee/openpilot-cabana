#include "signal_tree_model.h"

#include <QApplication>
#include <QFontMetrics>
#include <QMessageBox>
#include <QtConcurrent>
#include <array>
#include <span>

#include "core/commands/commands.h"
#include "core/dbc/dbc_manager.h"
#include "modules/inspector/binary/binary_model.h"
#include "modules/settings/settings.h"
#include "modules/system/stream_manager.h"

namespace {

// Property metadata for labels and grouping
struct PropertyInfo {
  SignalProperty prop;
  const char* label;
};

constexpr std::array kMainProperties = {
    PropertyInfo{SignalProperty::Name, "Name"},
    PropertyInfo{SignalProperty::Size, "Size"},
    PropertyInfo{SignalProperty::Node, "Receiver Nodes"},
    PropertyInfo{SignalProperty::Endian, "Little Endian"},
    PropertyInfo{SignalProperty::Signed, "Signed"},
    PropertyInfo{SignalProperty::Offset, "Offset"},
    PropertyInfo{SignalProperty::Factor, "Factor"},
    PropertyInfo{SignalProperty::SignalType, "Type"},
    PropertyInfo{SignalProperty::MultiplexValue, "Multiplex Value"},
    PropertyInfo{SignalProperty::ExtraInfo, "Extra Info"},
};

constexpr std::array kExtraProperties = {
    PropertyInfo{SignalProperty::Unit, "Unit"},
    PropertyInfo{SignalProperty::Comment, "Comment"},
    PropertyInfo{SignalProperty::Min, "Min"},
    PropertyInfo{SignalProperty::Max, "Max"},
    PropertyInfo{SignalProperty::ValueTable, "Value Table"},
};

QVariant getPropertyValue(const dbc::Signal* sig, SignalProperty prop) {
  switch (prop) {
    case SignalProperty::Name: return sig->name;
    case SignalProperty::Size: return sig->size;
    case SignalProperty::Node: return sig->receiver_name;
    case SignalProperty::SignalType: return signalTypeToString(sig->type);
    case SignalProperty::MultiplexValue: return sig->multiplex_value;
    case SignalProperty::Offset: return utils::doubleToString(sig->offset);
    case SignalProperty::Factor: return utils::doubleToString(sig->factor);
    case SignalProperty::Unit: return sig->unit;
    case SignalProperty::Comment: return sig->comment;
    case SignalProperty::Min: return utils::doubleToString(sig->min);
    case SignalProperty::Max: return utils::doubleToString(sig->max);
    case SignalProperty::ValueTable: {
      QStringList parts;
      for (const auto& [val, desc] : sig->value_table) {
        parts << QString::number(val) + " \"" + desc + "\"";
      }
      return parts.join(" ");
    }
    default: return {};
  }
}

bool setPropertyValue(dbc::Signal& sig, SignalProperty prop, const QVariant& value) {
  switch (prop) {
    case SignalProperty::Name: sig.name = value.toString(); return true;
    case SignalProperty::Size: sig.size = value.toInt(); return true;
    case SignalProperty::Node: sig.receiver_name = value.toString().trimmed(); return true;
    case SignalProperty::SignalType: sig.type = static_cast<dbc::Signal::Type>(value.toInt()); return true;
    case SignalProperty::MultiplexValue: sig.multiplex_value = value.toInt(); return true;
    case SignalProperty::Endian: sig.is_little_endian = value.toBool(); return true;
    case SignalProperty::Signed: sig.is_signed = value.toBool(); return true;
    case SignalProperty::Offset: sig.offset = value.toDouble(); return true;
    case SignalProperty::Factor: sig.factor = value.toDouble(); return true;
    case SignalProperty::Unit: sig.unit = value.toString(); return true;
    case SignalProperty::Comment: sig.comment = value.toString(); return true;
    case SignalProperty::Min: sig.min = value.toDouble(); return true;
    case SignalProperty::Max: sig.max = value.toDouble(); return true;
    case SignalProperty::ValueTable: sig.value_table = value.value<ValueTable>(); return true;
    default: return false;
  }
}

}  // namespace

QString signalTypeToString(dbc::Signal::Type type) {
  switch (type) {
    case dbc::Signal::Type::Multiplexor: return QStringLiteral("Multiplexor Signal");
    case dbc::Signal::Type::Multiplexed: return QStringLiteral("Multiplexed Signal");
    default: return QStringLiteral("Normal Signal");
  }
}

QString propertyLabel(SignalProperty prop) {
  for (const auto& info : kMainProperties) {
    if (info.prop == prop) return QString::fromLatin1(info.label);
  }
  for (const auto& info : kExtraProperties) {
    if (info.prop == prop) return QString::fromLatin1(info.label);
  }
  return {};
}

// --- SignalTreeModel ---

SignalTreeModel::SignalTreeModel(QObject* parent) : QAbstractItemModel(parent) {
  valueFont_ = qApp->font();
  root_ = std::make_unique<RootItem>();

  auto* dbc = GetDBC();
  connect(dbc, &dbc::Manager::DBCFileChanged, this, &SignalTreeModel::rebuild);
  connect(dbc, &dbc::Manager::msgUpdated, this, &SignalTreeModel::handleMsgChanged);
  connect(dbc, &dbc::Manager::msgRemoved, this, &SignalTreeModel::handleMsgChanged);
  connect(dbc, &dbc::Manager::signalAdded, this, &SignalTreeModel::handleSignalAdded);
  connect(dbc, &dbc::Manager::signalUpdated, this, &SignalTreeModel::handleSignalUpdated);
  connect(dbc, &dbc::Manager::signalRemoved, this, &SignalTreeModel::handleSignalRemoved);
}

void SignalTreeModel::setMessage(const MessageId& id) {
  msgId_ = id;
  filterStr_.clear();
  rebuild();
}

void SignalTreeModel::setFilter(const QString& txt) {
  filterStr_ = txt;
  rebuild();
}

void SignalTreeModel::rebuild() {
  resetSparklines();

  beginResetModel();
  root_ = std::make_unique<RootItem>();

  if (auto* msg = GetDBC()->msg(msgId_)) {
    auto sigs = msg->getSignals();
    root_->children.reserve(sigs.size());
    for (const auto* sig : sigs) {
      if (filterStr_.isEmpty() || sig->name.contains(filterStr_, Qt::CaseInsensitive)) {
        insertSignalItem(root_->children.size(), sig);
      }
    }
  }
  endResetModel();
}

void SignalTreeModel::insertSignalItem(int pos, const dbc::Signal* sig) {
  auto* item = new SignalItem(sig, root_.get());
  root_->children.insert(pos, item);
}

void SignalTreeModel::createPropertyChildren(TreeItem* parent, const dbc::Signal* sig) {
  std::span<const PropertyInfo> props;
  if (auto* propItem = dynamic_cast<PropertyItem*>(parent); propItem && propItem->isGroup()) {
    props = kExtraProperties;
  } else {
    props = kMainProperties;
  }

  parent->children.reserve(props.size());
  for (const auto& info : props) {
    parent->children.push_back(new PropertyItem(info.prop, sig, parent));
  }
}

TreeItem* SignalTreeModel::itemFromIndex(const QModelIndex& index) const {
  return index.isValid() ? static_cast<TreeItem*>(index.internalPointer()) : root_.get();
}

int SignalTreeModel::rowCount(const QModelIndex& parent) const {
  if (parent.column() > 0) return 0;
  return itemFromIndex(parent)->children.size();
}

bool SignalTreeModel::hasChildren(const QModelIndex& parent) const {
  if (!parent.isValid()) return true;

  auto* item = itemFromIndex(parent);
  if (item->nodeType() == NodeType::Signal) return true;
  if (auto* prop = dynamic_cast<PropertyItem*>(item)) return prop->isGroup();
  return false;
}

bool SignalTreeModel::canFetchMore(const QModelIndex& parent) const {
  if (!parent.isValid()) return false;

  auto* item = itemFromIndex(parent);
  if (!item->children.isEmpty()) return false;

  if (item->nodeType() == NodeType::Signal) return true;
  if (auto* prop = dynamic_cast<PropertyItem*>(item)) return prop->isGroup();
  return false;
}

void SignalTreeModel::fetchMore(const QModelIndex& parent) {
  if (!parent.isValid()) return;

  auto* item = itemFromIndex(parent);
  const dbc::Signal* sig = nullptr;

  if (auto* sigItem = dynamic_cast<SignalItem*>(item)) {
    sig = sigItem->sig;
  } else if (auto* propItem = dynamic_cast<PropertyItem*>(item)) {
    sig = propItem->sig;
  }

  if (!sig) return;

  int count = (dynamic_cast<PropertyItem*>(item) && dynamic_cast<PropertyItem*>(item)->isGroup())
                  ? kExtraProperties.size()
                  : kMainProperties.size();

  beginInsertRows(parent, 0, count - 1);
  createPropertyChildren(item, sig);
  endInsertRows();
}

QModelIndex SignalTreeModel::index(int row, int column, const QModelIndex& parent) const {
  if (parent.isValid() && parent.column() != 0) return {};

  auto* parentItem = itemFromIndex(parent);
  if (row >= 0 && row < parentItem->children.size()) {
    return createIndex(row, column, parentItem->children[row]);
  }
  return {};
}

QModelIndex SignalTreeModel::parent(const QModelIndex& index) const {
  if (!index.isValid()) return {};

  auto* item = itemFromIndex(index);
  auto* parentItem = item->parent;
  if (!parentItem || parentItem == root_.get()) return {};

  return createIndex(parentItem->row(), 0, parentItem);
}

Qt::ItemFlags SignalTreeModel::flags(const QModelIndex& index) const {
  if (!index.isValid()) return Qt::NoItemFlags;

  auto* item = itemFromIndex(index);
  Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;

  // Only property items in column 1 are editable (not signals or groups)
  auto* propItem = dynamic_cast<PropertyItem*>(item);
  if (index.column() == 1 && propItem && !propItem->isGroup()) {
    if (propItem->property == SignalProperty::Endian || propItem->property == SignalProperty::Signed) {
      flags |= Qt::ItemIsUserCheckable;
    } else {
      flags |= Qt::ItemIsEditable;
    }

    // Disable multiplex value for non-multiplexed signals
    if (propItem->property == SignalProperty::MultiplexValue &&
        propItem->sig->type != dbc::Signal::Type::Multiplexed) {
      flags &= ~Qt::ItemIsEnabled;
    }
  }

  return flags;
}

QVariant SignalTreeModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) return {};

  auto* item = itemFromIndex(index);

  if (role == Qt::DisplayRole || role == Qt::EditRole) {
    if (auto* sigItem = dynamic_cast<SignalItem*>(item)) {
      return index.column() == 0 ? sigItem->sig->name : sigItem->displayValue;
    }

    if (auto* propItem = dynamic_cast<PropertyItem*>(item)) {
      if (index.column() == 0) return propertyLabel(propItem->property);
      if (!propItem->isGroup()) return getPropertyValue(propItem->sig, propItem->property);
    }
    return {};
  }

  if (role == Qt::CheckStateRole && index.column() == 1) {
    if (auto* propItem = dynamic_cast<PropertyItem*>(item)) {
      if (propItem->property == SignalProperty::Endian) {
        return propItem->sig->is_little_endian ? Qt::Checked : Qt::Unchecked;
      }
      if (propItem->property == SignalProperty::Signed) {
        return propItem->sig->is_signed ? Qt::Checked : Qt::Unchecked;
      }
    }
  }

  if (role == Qt::ToolTipRole && index.column() == 0) {
    if (auto* sigItem = dynamic_cast<SignalItem*>(item)) {
      return signalToolTip(sigItem->sig);
    }
  }

  if (role == IsChartedRole) {
    if (auto* sigItem = dynamic_cast<SignalItem*>(item)) {
      auto it = chartedSignals_.find(msgId_);
      return (it != chartedSignals_.end()) && it.value().contains(sigItem->sig);
    }
  }

  return {};
}

bool SignalTreeModel::setData(const QModelIndex& index, const QVariant& value, int role) {
  if (role != Qt::EditRole && role != Qt::CheckStateRole) return false;

  auto* propItem = dynamic_cast<PropertyItem*>(itemFromIndex(index));
  if (!propItem || propItem->isGroup()) return false;

  dbc::Signal sig = *propItem->sig;
  if (!setPropertyValue(sig, propItem->property, value)) return false;

  bool success = saveSignal(propItem->sig, sig);
  emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, Qt::CheckStateRole});
  return success;
}

int SignalTreeModel::signalRow(const dbc::Signal* sig) const {
  for (int i = 0; i < root_->children.size(); ++i) {
    if (auto* sigItem = dynamic_cast<SignalItem*>(root_->children[i])) {
      if (sigItem->sig == sig) return i;
    }
  }
  return -1;
}

void SignalTreeModel::updateValues(const MessageSnapshot* msg) {
  QFontMetrics fm(valueFont_);
  int currentMax = 0;

  for (auto* child : root_->children) {
    auto* sigItem = dynamic_cast<SignalItem*>(child);
    if (!sigItem) continue;

    if (msg->size == 0) {
      sigItem->displayValue = QStringLiteral("-");
    } else {
      double val = 0;
      if (sigItem->sig->parse(msg->data.data(), msg->size, &val)) {
        sigItem->displayValue = sigItem->sig->formatValue(val);
      }
    }
    sigItem->valueWidth = fm.horizontalAdvance(sigItem->displayValue);
    currentMax = std::max(currentMax, sigItem->valueWidth);
  }

  currentMax += 10;
  if (currentMax > maxValueWidth_ || maxValueWidth_ - currentMax > 40) {
    maxValueWidth_ = currentMax;
  }
}

void SignalTreeModel::updateSparklines(const MessageSnapshot* msg, int firstRow, int lastRow, const QSize& size) {
  if (msg->size == 0) {
    for (auto* child : root_->children) {
      if (auto* sigItem = dynamic_cast<SignalItem*>(child)) {
        sigItem->sparkline->clearHistory();
      }
    }
    emit dataChanged(index(firstRow, 1), index(lastRow, 1), {Qt::DisplayRole});
    return;
  }

  auto* stream = StreamManager::stream();
  const uint64_t currentNs = stream->toMonoNs(msg->ts);

  // Detect seek or gap > 1s
  bool jumpDetected = (prevSparklineNs_ != 0) &&
                      ((currentNs < prevSparklineNs_) || (currentNs > prevSparklineNs_ + 1000000000ULL));
  bool timeShifted = (currentNs != prevSparklineNs_);
  bool sizeChanged = (size != prevSparklineSize_);

  // Collect visible items and check staleness
  QVector<SignalItem*> items;
  items.reserve(lastRow - firstRow + 1);
  bool hasStaleItems = false;

  for (int i = firstRow; i <= lastRow; ++i) {
    if (auto* sigItem = dynamic_cast<SignalItem*>(itemFromIndex(index(i, 1)))) {
      items << sigItem;
      if (!sigItem->sparkline->isUpToDate(currentNs)) hasStaleItems = true;
    }
  }

  if (!timeShifted && !sizeChanged && !jumpDetected && !hasStaleItems) return;

  if (jumpDetected) {
    for (auto* child : root_->children) {
      if (auto* sigItem = dynamic_cast<SignalItem*>(child)) {
        sigItem->sparkline->clearHistory();
      }
    }
  }

  prevSparklineNs_ = currentNs;
  prevSparklineSize_ = size;

  const uint64_t rangeNs = static_cast<uint64_t>(settings.sparkline_range) * 1000000000ULL;
  uint64_t winStart = (currentNs > rangeNs) ? (currentNs - rangeNs) : 0;
  auto range =
      stream->eventsInRange(msgId_, std::make_pair(stream->toSeconds(winStart), stream->toSeconds(currentNs)));

  QtConcurrent::blockingMap(items, [&](SignalItem* item) {
    item->sparkline->update(item->sig, range.first, range.second, currentNs, settings.sparkline_range, size);
  });

  emit dataChanged(index(firstRow, 1), index(lastRow, 1), {Qt::DisplayRole});
}

void SignalTreeModel::updateChartedSignals(const QMap<MessageId, QSet<const dbc::Signal*>>& opened) {
  chartedSignals_ = opened;
  if (rowCount() > 0) {
    emit dataChanged(index(0, 0), index(rowCount() - 1, 1), {IsChartedRole});
  }
}

void SignalTreeModel::highlightSignalRow(const dbc::Signal* sig) {
  for (int i = 0; i < root_->children.size(); ++i) {
    auto* sigItem = dynamic_cast<SignalItem*>(root_->children[i]);
    if (!sigItem) continue;

    bool highlight = (sigItem->sig == sig);
    if (sigItem->highlight != highlight) {
      sigItem->highlight = highlight;
      emit dataChanged(index(i, 0), index(i, 1), {Qt::DecorationRole, Qt::DisplayRole});
    }
  }
}

bool SignalTreeModel::saveSignal(const dbc::Signal* origin, dbc::Signal& updated) {
  auto* msg = GetDBC()->msg(msgId_);
  if (updated.name != origin->name && msg->sig(updated.name) != nullptr) {
    QString text = tr("There is already a signal with the same name '%1'").arg(updated.name);
    QMessageBox::warning(nullptr, tr("Failed to save signal"), text);
    return false;
  }

  if (updated.is_little_endian != origin->is_little_endian) {
    updated.start_bit = flipBitPos(updated.start_bit);
  }
  UndoStack::push(new EditSignalCommand(msgId_, origin, updated));
  return true;
}

void SignalTreeModel::resetSparklines() {
  prevSparklineNs_ = 0;
  prevSparklineSize_ = {};
}

void SignalTreeModel::handleMsgChanged(MessageId id) {
  if (id.address == msgId_.address) {
    rebuild();
  }
}

void SignalTreeModel::handleSignalAdded(MessageId id, const dbc::Signal* sig) {
  if (id != msgId_) return;

  if (filterStr_.isEmpty()) {
    int pos = GetDBC()->msg(msgId_)->indexOf(sig);
    beginInsertRows({}, pos, pos);
    insertSignalItem(pos, sig);
    endInsertRows();
  } else if (sig->name.contains(filterStr_, Qt::CaseInsensitive)) {
    rebuild();
  }
}

void SignalTreeModel::handleSignalUpdated(const dbc::Signal* sig) {
  int row = signalRow(sig);
  if (row == -1) return;

  emit dataChanged(index(row, 0), index(row, 1), {Qt::DisplayRole, Qt::EditRole, Qt::CheckStateRole});

  if (filterStr_.isEmpty()) {
    int targetRow = GetDBC()->msg(msgId_)->indexOf(sig);
    if (targetRow != row) {
      beginMoveRows({}, row, row, {}, targetRow > row ? targetRow + 1 : targetRow);
      root_->children.move(row, targetRow);
      endMoveRows();
    }
  }
}

void SignalTreeModel::handleSignalRemoved(const dbc::Signal* sig) {
  int row = signalRow(sig);
  if (row != -1) {
    beginRemoveRows({}, row, row);
    delete root_->children.takeAt(row);
    endRemoveRows();
  }
}
