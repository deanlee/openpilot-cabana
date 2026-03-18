#pragma once

#include <QAbstractItemModel>
#include <QSet>
#include <memory>

#include "core/dbc/dbc_message.h"
#include "modules/charts/sparkline.h"

enum SignalRole { IsChartedRole = Qt::UserRole + 10 };

// Forward declaration
class SignalTreeModel;

// Property identifiers for signal fields
enum class SignalProperty {
  Name,
  Size,
  Node,
  Endian,
  Signed,
  Offset,
  Factor,
  SignalType,
  MultiplexValue,
  ExtraInfo,  // Group header
  Unit,
  Comment,
  Min,
  Max,
  ValueTable
};

// Tree node types
enum class NodeType { Root, Signal, Property };

// Base tree item
struct TreeItem {
  virtual ~TreeItem() { qDeleteAll(children); }
  virtual NodeType nodeType() const = 0;

  int row() const { return parent ? parent->children.indexOf(const_cast<TreeItem*>(this)) : 0; }

  TreeItem* parent = nullptr;
  QVector<TreeItem*> children;
};

// Signal row item - top level, owns sparkline
struct SignalItem : TreeItem {
  explicit SignalItem(const dbc::Signal* s, TreeItem* p) : sig(s) {
    parent = p;
    sparkline = std::make_unique<Sparkline>();
  }

  NodeType nodeType() const override { return NodeType::Signal; }

  const dbc::Signal* sig;
  bool highlight = false;
  QString displayValue = QStringLiteral("-");
  int valueWidth = 0;
  std::unique_ptr<Sparkline> sparkline;
};

// Property row item - child of signal, holds property type
struct PropertyItem : TreeItem {
  PropertyItem(SignalProperty prop, const dbc::Signal* s, TreeItem* p) : property(prop), sig(s) { parent = p; }

  NodeType nodeType() const override { return NodeType::Property; }
  bool isGroup() const { return property == SignalProperty::ExtraInfo; }

  SignalProperty property;
  const dbc::Signal* sig;
};

// Root item
struct RootItem : TreeItem {
  NodeType nodeType() const override { return NodeType::Root; }
};

class SignalTreeModel : public QAbstractItemModel {
  Q_OBJECT
 public:
  SignalTreeModel(QObject* parent);

  // Public API
  void setMessage(const MessageId& id);
  void setFilter(const QString& txt);
  void updateValues(const MessageSnapshot* msg);
  void updateSparklines(const MessageSnapshot* msg, int firstRow, int lastRow, const QSize& size);
  void updateChartedSignals(const QMap<MessageId, QSet<const dbc::Signal*>>& opened);
  void highlightSignalRow(const dbc::Signal* sig);
  void resetSparklines();
  bool saveSignal(const dbc::Signal* origin, dbc::Signal& updated);

  // Accessors
  const MessageId& messageId() const { return msgId_; }
  int maxValueWidth() const { return maxValueWidth_; }
  int signalRow(const dbc::Signal* sig) const;
  TreeItem* itemFromIndex(const QModelIndex& index) const;

  // QAbstractItemModel interface
  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override { return 2; }
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
  QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex& index) const override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;
  void fetchMore(const QModelIndex& parent) override;
  bool canFetchMore(const QModelIndex& parent) const override;

 private:
  bool hasChildren(const QModelIndex& parent) const override;
  void rebuild();
  void insertSignalItem(int pos, const dbc::Signal* sig);
  void createPropertyChildren(TreeItem* parent, const dbc::Signal* sig);

  // Signal handlers
  void handleSignalAdded(MessageId id, const dbc::Signal* sig);
  void handleSignalUpdated(const dbc::Signal* sig);
  void handleSignalRemoved(const dbc::Signal* sig);
  void handleMsgChanged(MessageId id);

  // Data
  MessageId msgId_;
  QString filterStr_;
  std::unique_ptr<RootItem> root_;
  QMap<MessageId, QSet<const dbc::Signal*>> chartedSignals_;

  // Display state
  QFont valueFont_;
  int maxValueWidth_ = 0;
  uint64_t prevSparklineNs_ = 0;
  QSize prevSparklineSize_;
};

// Utilities
QString signalTypeToString(dbc::Signal::Type type);
QString propertyLabel(SignalProperty prop);
