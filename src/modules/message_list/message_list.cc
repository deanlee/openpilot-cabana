#include "message_list.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

#include "common.h"
#include "core/commands/commands.h"
#include "modules/settings/settings.h"
#include "modules/system/stream_manager.h"
#include "widgets/tool_button.h"

MessageList::MessageList(QWidget* parent) : QWidget(parent) {
  QVBoxLayout* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);

  table_ = new MessageTable(this);
  model_ = new MessageModel(this);
  header_ = new MessageHeader(this);
  delegate_ = new MessageDelegate(table_, CallerType::MessageList);
  menu_ = new QMenu(this);

  table_->setItemDelegate(delegate_);
  table_->setModel(model_);  // Set model before configuring header sections
  table_->setHeader(header_);

  header_->blockSignals(true);
  header_->setSectionsMovable(true);
  header_->setContextMenuPolicy(Qt::CustomContextMenu);
  header_->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  header_->setStretchLastSection(true);

  header_->blockSignals(false);
  header_->setSortIndicator(MessageModel::Column::NAME, Qt::AscendingOrder);

  restoreHeaderState(settings.message_header_state);

  main_layout->addWidget(createToolBar());
  main_layout->addWidget(table_);

  setWhatsThis(tr(R"(
    <b>Message View</b><br/>
    <span style="color:gray">Byte color</span><br />
    <span style="color:gray;">■ </span> constant changing<br />
    <span style="color:blue;">■ </span> increasing<br />
    <span style="color:red;">■ </span> decreasing<br />
    <span style="color:gray">Shortcuts</span><br />
    Horizontal Scrolling: <span style="background-color:lightGray;color:gray">&nbsp;shift+wheel&nbsp;</span>
  )"));

  setupConnections();
}

QWidget* MessageList::createToolBar() {
  QWidget* toolbar = new QWidget(this);
  QHBoxLayout* layout = new QHBoxLayout(toolbar);
  layout->setContentsMargins(0, 4, 0, 0);
  layout->setSpacing(style()->pixelMetric(QStyle::PM_ToolBarItemSpacing));

  layout->addWidget(mute_active_btn_ = new ToolButton("ban"));
  mute_active_btn_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  mute_active_btn_->setText(tr("Mute Active"));
  mute_active_btn_->setToolTip(
      tr("Mute Active Bits\n"
         "Silences currently changing bits to isolate new transitions."));
  layout->addWidget(unmute_all_btn_ = new ToolButton("refresh-ccw"));
  unmute_all_btn_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  unmute_all_btn_->setText(tr("Unmute All"));
  unmute_all_btn_->setToolTip(
      tr("Unmute All Bits\n"
         "Restores highlighting for all data bits."));

  suppress_defined_signals_ = new QCheckBox(tr("Mute Defined"), this);
  suppress_defined_signals_->setFocusPolicy(Qt::NoFocus);
  suppress_defined_signals_->setToolTip(
      tr("Mute Defined Signals\n"
         "Silences bits already assigned to DBC signals to focus on unknown data."));
  layout->addWidget(suppress_defined_signals_);

  layout->addStretch(1);
  auto view_button = new ToolButton("ellipsis", tr("View..."));
  view_button->setMenu(menu_);
  view_button->setPopupMode(QToolButton::InstantPopup);
  view_button->setStyleSheet("QToolButton::menu-indicator { image: none; }");
  layout->addWidget(view_button);

  connect(mute_active_btn_, &ToolButton::clicked, this, [this]() { suppressHighlighted(true); });
  connect(unmute_all_btn_, &ToolButton::clicked, this, [this]() { suppressHighlighted(false); });
  connect(suppress_defined_signals_, &QCheckBox::stateChanged, this,
          [this]() { StreamManager::stream()->suppressDefinedSignals(suppress_defined_signals_->isChecked()); });

  suppressHighlighted(false);
  return toolbar;
}

void MessageList::setupConnections() {
  connect(menu_, &QMenu::aboutToShow, this, &MessageList::onMenuAboutToShow);
  connect(header_, &MessageHeader::customContextMenuRequested, this, &MessageList::onHeaderContextMenuRequested);
  connect(table_->horizontalScrollBar(), &QScrollBar::valueChanged, header_, &MessageHeader::updateHeaderPositions);
  connect(&StreamManager::instance(), &StreamManager::snapshotsUpdated, model_, &MessageModel::onSnapshotsUpdated);
  connect(&StreamManager::instance(), &StreamManager::streamChanged, this, &MessageList::resetState);
  connect(GetDBC(), &dbc::Manager::DBCFileChanged, model_, &MessageModel::rebuild);
  connect(UndoStack::instance(), &QUndoStack::indexChanged, model_, &MessageModel::rebuild);
  connect(table_->selectionModel(), &QItemSelectionModel::currentChanged, this, &MessageList::onCurrentChanged);
  connect(model_, &MessageModel::modelReset, [this]() {
    if (current_msg_id_) {
      selectMessageForced(*current_msg_id_, true);
    }
    updateTitle();
  });
}

void MessageList::suppressHighlighted(bool suppress) {
  int n = 0;
  if (suppress) {
    n = StreamManager::stream()->suppressHighlighted();
  } else {
    StreamManager::stream()->clearSuppressed();
  }
  unmute_all_btn_->setText(n > 0 ? tr("Unmute (%1 bits)").arg(n) : tr("Unmute All"));
  unmute_all_btn_->setEnabled(n > 0);
}

void MessageList::selectMessageForced(const MessageId& msg_id, bool force) {
  if (!force && current_msg_id_ && *current_msg_id_ == msg_id) return;

  int row = model_->getRowForMessageId(msg_id);
  if (row != -1) {
    QModelIndex index = model_->index(row, 0);
    table_->setCurrentIndex(index);
    table_->scrollTo(index, QAbstractItemView::PositionAtCenter);
  }
}

void MessageList::resetState() {
  current_msg_id_.reset();
  if (table_->selectionModel()) {
    table_->selectionModel()->clearSelection();
    table_->selectionModel()->clearCurrentIndex();
  }
  model_->rebuild();

  unmute_all_btn_->setText(tr("Unmute All"));
  unmute_all_btn_->setEnabled(false);

  updateTitle();
  table_->scrollToTop();
}

void MessageList::updateTitle() {
  emit titleChanged(tr("%1 Messages (%2 DBC Messages, %3 Signals)")
                        .arg(model_->rowCount())
                        .arg(model_->getDbcMessageCount())
                        .arg(model_->getSignalCount()));
}

void MessageList::onCurrentChanged(const QModelIndex& current) {
  if (current.isValid()) {
    auto* item = model_->getItem(current);
    if (!current_msg_id_ || item->id != *current_msg_id_) {
      current_msg_id_ = item->id;
      emit msgSelectionChanged(*current_msg_id_);
    }
  }
}

void MessageList::onHeaderContextMenuRequested(const QPoint& pos) { menu_->exec(header_->mapToGlobal(pos)); }

void MessageList::onMenuAboutToShow() {
  menu_->clear();
  for (int i = 0; i < header_->count(); ++i) {
    int logical_index = header_->logicalIndex(i);
    auto action = menu_->addAction(model_->headerData(logical_index, Qt::Horizontal).toString(),
                                  [=](bool checked) { header_->setSectionHidden(logical_index, !checked); });
    action->setCheckable(true);
    action->setChecked(!header_->isSectionHidden(logical_index));
    // Can't hide the name column
    action->setEnabled(logical_index > 0);
  }
  menu_->addSeparator();

  auto* action = menu_->addAction(tr("Show inactive Messages"), model_, &MessageModel::setInactiveMessagesVisible);
  action->setCheckable(true);
  action->setChecked(model_->isInactiveMessagesVisible());
}
