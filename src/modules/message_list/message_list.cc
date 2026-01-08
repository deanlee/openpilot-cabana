#include "message_list.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QVBoxLayout>

#include "common.h"
#include "core/commands/commands.h"
#include "modules/settings/settings.h"

MessageList::MessageList(QWidget *parent) : menu(new QMenu(this)), QWidget(parent) {
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  // toolbar
  main_layout->addWidget(createToolBar());
  // message table
  main_layout->addWidget(view = new MessageTable(this));
  view->setItemDelegate(delegate = new MessageDelegate(view, settings.multiple_lines_hex));
  view->setModel(model = new MessageModel(this));
  view->setHeader(header = new MessageHeader(this));

  // Must be called before setting any header parameters to avoid overriding
  restoreHeaderState(settings.message_header_state);
  header->setSectionsMovable(true);
  header->setSectionResizeMode(MessageModel::Column::DATA, QHeaderView::Fixed);
  header->setStretchLastSection(true);
  header->setContextMenuPolicy(Qt::CustomContextMenu);

  // signals/slots
  connect(menu, &QMenu::aboutToShow, this, &MessageList::menuAboutToShow);
  connect(header, &MessageHeader::customContextMenuRequested, this, &MessageList::headerContextMenuEvent);
  connect(view->horizontalScrollBar(), &QScrollBar::valueChanged, header, &MessageHeader::updateHeaderPositions);
  connect(can, &AbstractStream::snapshotsUpdated, model, &MessageModel::onSnapshotsUpdated);
  connect(GetDBC(), &dbc::Manager::DBCFileChanged, model, &MessageModel::dbcModified);
  connect(UndoStack::instance(), &QUndoStack::indexChanged, model, &MessageModel::dbcModified);
  connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this, &MessageList::handleSelectionChanged);
  connect(model, &MessageModel::modelReset, [this]() {
    if (current_msg_id) {
      selectMessage(*current_msg_id);
    }
    view->updateLayout();
    updateTitle();
  });

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
}

QWidget *MessageList::createToolBar() {
  QWidget *toolbar = new QWidget(this);
  QHBoxLayout *layout = new QHBoxLayout(toolbar);
  layout->setContentsMargins(0, 9, 0, 0);
  layout->addWidget(suppress_add = new QPushButton("Suppress Highlighted"));
  suppress_add->setToolTip(tr("Mute activity for selected bits.\n"
                            "This hides color changes in the highlighted area to focus on other transitions."));
  layout->addWidget(suppress_clear = new QPushButton());
  suppress_clear->setToolTip(tr("Clear all suppressed bits.\n"
                            "Restores activity highlighting for all previously muted bits."));
  layout->addStretch(1);
  QCheckBox *suppress_defined_signals = new QCheckBox(tr("Suppress Signals"), this);
  suppress_defined_signals->setToolTip(tr("Mute activity for all bits already assigned to signals.\n"
                                        "Helps isolate unknown bit transitions in the message."));

  suppress_defined_signals->setChecked(settings.suppress_defined_signals);
  layout->addWidget(suppress_defined_signals);

  auto view_button = new ToolButton("ellipsis", tr("View..."));
  view_button->setMenu(menu);
  view_button->setPopupMode(QToolButton::InstantPopup);
  view_button->setStyleSheet("QToolButton::menu-indicator { image: none; }");
  layout->addWidget(view_button);

  connect(suppress_add, &QPushButton::clicked, this, &MessageList::suppressHighlighted);
  connect(suppress_clear, &QPushButton::clicked, this, &MessageList::suppressHighlighted);
  connect(suppress_defined_signals, &QCheckBox::stateChanged, can, &AbstractStream::suppressDefinedSignals);

  suppressHighlighted();
  return toolbar;
}

void MessageList::updateTitle() {
  auto stats = std::accumulate(
      model->items_.begin(), model->items_.end(), std::pair<size_t, size_t>(),
      [](const auto &pair, const auto &item) {
        auto m = GetDBC()->msg(item.id);
        return m ? std::make_pair(pair.first + 1, pair.second + m->sigs.size()) : pair;
      });
  emit titleChanged(tr("%1 Messages (%2 DBC Messages, %3 Signals)")
                      .arg(model->items_.size()).arg(stats.first).arg(stats.second));
}

void MessageList::handleSelectionChanged(const QModelIndex &current) {
  if (current.isValid() && current.row() < model->items_.size()) {
    const auto &id = model->items_[current.row()].id;
    if (!current_msg_id || id != *current_msg_id) {
      current_msg_id = id;
      emit msgSelectionChanged(*current_msg_id);
    }
  }
}

void MessageList::selectMessage(const MessageId &msg_id) {
  auto it = std::find_if(model->items_.cbegin(), model->items_.cend(),
                         [&msg_id](auto &item) { return item.id == msg_id; });
  if (it != model->items_.cend()) {
    view->setCurrentIndex(model->index(std::distance(model->items_.cbegin(), it), 0));
  }
}

void MessageList::suppressHighlighted() {
  int n = sender() == suppress_add ? can->suppressHighlighted() : (can->clearSuppressed(), 0);
  suppress_clear->setText(n > 0 ? tr("Clear (%1)").arg(n) : tr("Clear"));
  suppress_clear->setEnabled(n > 0);
}

void MessageList::headerContextMenuEvent(const QPoint &pos) {
  menu->exec(header->mapToGlobal(pos));
}

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
  auto action = menu->addAction(tr("Multi-Line bytes"), this, &MessageList::setMultiLineBytes);
  action->setCheckable(true);
  action->setChecked(settings.multiple_lines_hex);

  action = menu->addAction(tr("Show inactive Messages"), model, &MessageModel::showInactivemessages);
  action->setCheckable(true);
  action->setChecked(model->show_inactive_messages);
}

void MessageList::setMultiLineBytes(bool multi) {
  settings.multiple_lines_hex = multi;
  view->updateLayout();
}
