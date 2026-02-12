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

  view = new MessageTable(this);
  model = new MessageModel(this);
  header = new MessageHeader(this);
  delegate = new MessageDelegate(view, CallerType::MessageList);
  menu = new QMenu(this);

  view->setItemDelegate(delegate);
  view->setModel(model);  // Set model before configuring header sections
  view->setHeader(header);

  header->blockSignals(true);
  header->setSectionsMovable(true);
  header->setContextMenuPolicy(Qt::CustomContextMenu);
  header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  header->setStretchLastSection(true);

  header->blockSignals(false);
  header->setSortIndicator(MessageModel::Column::NAME, Qt::AscendingOrder);

  restoreHeaderState(settings.message_header_state);

  main_layout->addWidget(createToolBar());
  main_layout->addWidget(view);

  setWhatsThis(tr(R"(
    <b>Message View</b><br/>
    <!-- TODO: add descprition here -->
    <span style="color:gray">Byte color</span><br />
    <span style="color:gray;">■ </span> constant changing<br />
    <span style="color:blue;">■ </span> increasing<br />
    <span style="color:red;">■ </span> decreasing<br />
    <span style="color:gray">Shortcuts</span><br />
    Horizontal Scrolling: <span style="background-color:lightGray;color:gray">&nbsp;shift+wheel&nbsp;</span>
  )"));

  setupConnections();
}

void MessageList::setupConnections() {
  connect(menu, &QMenu::aboutToShow, this, &MessageList::menuAboutToShow);
  connect(header, &MessageHeader::customContextMenuRequested, this, &MessageList::headerContextMenuEvent);
  connect(view->horizontalScrollBar(), &QScrollBar::valueChanged, header, &MessageHeader::updateHeaderPositions);
  connect(&StreamManager::instance(), &StreamManager::snapshotsUpdated, model, &MessageModel::onSnapshotsUpdated);
  connect(&StreamManager::instance(), &StreamManager::streamChanged, this, &MessageList::resetState);
  connect(GetDBC(), &dbc::Manager::DBCFileChanged, model, &MessageModel::rebuild);
  connect(UndoStack::instance(), &QUndoStack::indexChanged, model, &MessageModel::rebuild);
  connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this, &MessageList::handleSelectionChanged);
  connect(model, &MessageModel::modelReset, [this]() {
    if (current_msg_id) {
      selectMessageForced(*current_msg_id, true);
    }
    updateTitle();
  });
}

QWidget* MessageList::createToolBar() {
  QWidget* toolbar = new QWidget(this);
  QHBoxLayout* layout = new QHBoxLayout(toolbar);
  layout->setContentsMargins(0, 4, 0, 0);
  layout->setSpacing(style()->pixelMetric(QStyle::PM_ToolBarItemSpacing));

  layout->addWidget(suppress_add = new ToolButton("ban"));
  suppress_add->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  suppress_add->setText(tr("Mute Current"));
  suppress_add->setToolTip(
      tr("Mute Current Activity.\n"
         "Silences bytes currently changing to help you detect new bit transitions."));
  layout->addWidget(suppress_clear = new ToolButton("refresh-ccw"));
  suppress_clear->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  suppress_clear->setText(tr("Reset Activity"));
  suppress_clear->setToolTip(
      tr("Reset Activity.\n"
         "Restore highlighting for all bytes."));

  suppress_defined_signals = new QCheckBox(tr("Mute Defined"), this);
  suppress_defined_signals->setFocusPolicy(Qt::NoFocus);
  suppress_defined_signals->setToolTip(
      tr("Mute Defined Signals.\n"
         "Focus on unknown data by hiding activity for bits already assigned to a signal."));
  layout->addWidget(suppress_defined_signals);

  layout->addStretch(1);
  auto view_button = new ToolButton("ellipsis", tr("View..."));
  view_button->setMenu(menu);
  view_button->setPopupMode(QToolButton::InstantPopup);
  view_button->setStyleSheet("QToolButton::menu-indicator { image: none; }");
  layout->addWidget(view_button);

  connect(suppress_add, &ToolButton::clicked, this, [this]() { suppressHighlighted(true); });
  connect(suppress_clear, &ToolButton::clicked, this, [this]() { suppressHighlighted(false); });
  connect(suppress_defined_signals, &QCheckBox::stateChanged, this,
          [this]() { StreamManager::stream()->suppressDefinedSignals(suppress_defined_signals->isChecked()); });

  suppressHighlighted(false);
  return toolbar;
}

void MessageList::resetState() {
  current_msg_id.reset();
  if (view->selectionModel()) {
    view->selectionModel()->clearSelection();
    view->selectionModel()->clearCurrentIndex();
  }
  model->rebuild();

  suppress_clear->setText(tr("Reset Activity"));
  suppress_clear->setEnabled(false);

  updateTitle();
  view->scrollToTop();
}

void MessageList::updateTitle() {
  emit titleChanged(tr("%1 Messages (%2 DBC Messages, %3 Signals)")
                        .arg(model->rowCount())
                        .arg(model->getDbcMessageCount())
                        .arg(model->getSignalCount()));
}

void MessageList::handleSelectionChanged(const QModelIndex& current) {
  if (current.isValid()) {
    auto* item = model->getItem(current);
    if (!current_msg_id || item->id != *current_msg_id) {
      current_msg_id = item->id;
      emit msgSelectionChanged(*current_msg_id);
    }
  }
}

void MessageList::selectMessageForced(const MessageId& msg_id, bool force) {
  if (!force && current_msg_id && *current_msg_id == msg_id) return;

  int row = model->getRowForMessageId(msg_id);
  if (row != -1) {
    QModelIndex index = model->index(row, 0);
    view->setCurrentIndex(index);
    view->scrollTo(index, QAbstractItemView::PositionAtCenter);
  }
}

void MessageList::suppressHighlighted(bool suppress) {
  int n = 0;
  if (suppress) {
    n = StreamManager::stream()->suppressHighlighted();
  } else {
    StreamManager::stream()->clearSuppressed();
  }
  suppress_clear->setText(n > 0 ? tr("Reset Activity (%1)").arg(n) : tr("Reset Activity"));
  suppress_clear->setEnabled(n > 0);
}

void MessageList::headerContextMenuEvent(const QPoint& pos) { menu->exec(header->mapToGlobal(pos)); }

void MessageList::menuAboutToShow() {
  menu->clear();
  for (int i = 0; i < header->count(); ++i) {
    int logical_index = header->logicalIndex(i);
    auto action = menu->addAction(model->headerData(logical_index, Qt::Horizontal).toString(),
                                  [=](bool checked) { header->setSectionHidden(logical_index, !checked); });
    action->setCheckable(true);
    action->setChecked(!header->isSectionHidden(logical_index));
    // Can't hide the name column
    action->setEnabled(logical_index > 0);
  }
  menu->addSeparator();

  auto* action = menu->addAction(tr("Show inactive Messages"), model, &MessageModel::setInactiveMessagesVisible);
  action->setCheckable(true);
  action->setChecked(model->isInactiveMessagesVisible());
}
