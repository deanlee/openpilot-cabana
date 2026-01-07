#include "message_list.h"

#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

#include "commands.h"
#include "common.h"
#include "settings.h"

MessageList::MessageList(QWidget *parent) : menu(new QMenu(this)), QWidget(parent) {
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  // toolbar
  main_layout->addWidget(createToolBar());
  // message table
  main_layout->addWidget(view = new MessageTable(this));
  view->setItemDelegate(delegate = new MessageTableDelegate(view, settings.multiple_lines_hex));
  view->setModel(model = new MessageTableModel(this));
  view->setHeader(header = new MessageHeader(this));
  view->setSortingEnabled(true);
  view->sortByColumn(MessageTableModel::Column::NAME, Qt::AscendingOrder);
  view->setAllColumnsShowFocus(true);
  view->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view->setItemsExpandable(false);
  view->setIndentation(0);
  view->setRootIsDecorated(false);
  view->setAlternatingRowColors(true);

  // Must be called before setting any header parameters to avoid overriding
  restoreHeaderState(settings.message_header_state);
  header->setSectionsMovable(true);
  header->setSectionResizeMode(MessageTableModel::Column::DATA, QHeaderView::Fixed);
  header->setStretchLastSection(true);
  header->setContextMenuPolicy(Qt::CustomContextMenu);

  // signals/slots
  connect(menu, &QMenu::aboutToShow, this, &MessageList::menuAboutToShow);
  connect(header, &MessageHeader::customContextMenuRequested, this, &MessageList::headerContextMenuEvent);
  connect(view->horizontalScrollBar(), &QScrollBar::valueChanged, header, &MessageHeader::updateHeaderPositions);
  connect(can, &AbstractStream::snapshotsUpdated, model, &MessageTableModel::onSnapshotsUpdated);
  connect(GetDBC(), &dbc::Manager::DBCFileChanged, model, &MessageTableModel::dbcModified);
  connect(UndoStack::instance(), &QUndoStack::indexChanged, model, &MessageTableModel::dbcModified);
  connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this, &MessageList::handleSelectionChanged);
  connect(model, &MessageTableModel::modelReset, [this]() {
    if (current_msg_id) {
      selectMessage(*current_msg_id);
    }
    view->updateBytesSectionSize();
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

  action = menu->addAction(tr("Show inactive Messages"), model, &MessageTableModel::showInactivemessages);
  action->setCheckable(true);
  action->setChecked(model->show_inactive_messages);
}

void MessageList::setMultiLineBytes(bool multi) {
  settings.multiple_lines_hex = multi;
  delegate->setMultipleLines(multi);
  view->updateBytesSectionSize();
  view->doItemsLayout();
}

// MessageTable

void MessageTable::dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles) {
  // Bypass the slow call to QTreeView::dataChanged.
  // QTreeView::dataChanged will invalidate the height cache and that's what we don't need in MessageTable.
  QAbstractItemView::dataChanged(topLeft, bottomRight, roles);
}

void MessageTable::updateBytesSectionSize() {
  auto delegate = ((MessageTableDelegate *)itemDelegate());
  int max_bytes = 8;
  if (!delegate->multipleLines()) {
    for (const auto &[_, m] : can->snapshots()) {
      max_bytes = std::max<int>(max_bytes, m->dat.size());
    }
  }
  setUniformRowHeights(!delegate->multipleLines());
  header()->resizeSection(MessageTableModel::Column::DATA, delegate->sizeForBytes(max_bytes).width());
}

void MessageTable::wheelEvent(QWheelEvent *event) {
  if (event->modifiers() == Qt::ShiftModifier) {
    QApplication::sendEvent(horizontalScrollBar(), event);
  } else {
    QTreeView::wheelEvent(event);
  }
}

// MessageHeader

MessageHeader::MessageHeader(QWidget *parent) : QHeaderView(Qt::Horizontal, parent) {
  filter_timer.setSingleShot(true);
  filter_timer.setInterval(300);
  connect(&filter_timer, &QTimer::timeout, this, &MessageHeader::updateFilters);
  connect(this, &QHeaderView::sectionResized, this, &MessageHeader::updateHeaderPositions);
  connect(this, &QHeaderView::sectionMoved, this, &MessageHeader::updateHeaderPositions);
}

void MessageHeader::updateFilters() {
  QMap<int, QString> filters;
  for (int i = 0; i < count(); i++) {
    if (editors[i] && !editors[i]->text().isEmpty()) {
      filters[i] = editors[i]->text();
    }
  }
  qobject_cast<MessageTableModel*>(model())->setFilterStrings(filters);
}

void MessageHeader::updateHeaderPositions() {
  QSize sz = QHeaderView::sizeHint();
  for (int i = 0; i < count(); i++) {
    if (editors[i]) {
      int h = editors[i]->sizeHint().height();
      editors[i]->setGeometry(sectionViewportPosition(i), sz.height(), sectionSize(i), h);
      editors[i]->setHidden(isSectionHidden(i));
    }
  }
}

void MessageHeader::updateGeometries() {
  for (int i = 0; i < count(); i++) {
    if (!editors[i]) {
      QString column_name = model()->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString();
      editors[i] = new QLineEdit(this);
      editors[i]->setClearButtonEnabled(true);
      editors[i]->setPlaceholderText(tr("Filter %1").arg(column_name));

      connect(editors[i], &QLineEdit::textChanged, [this](const QString& text) {
        if (text.isEmpty()) {
          filter_timer.stop();
          updateFilters();  // Instant clear
        } else {
          filter_timer.start();  // Debounced search
        }
      });
    }
  }
  setViewportMargins(0, 0, 0, editors[0] ? editors[0]->sizeHint().height() : 0);

  QHeaderView::updateGeometries();
  updateHeaderPositions();
}

QSize MessageHeader::sizeHint() const {
  QSize sz = QHeaderView::sizeHint();
  return editors[0] ? QSize(sz.width(), sz.height() + editors[0]->height() + 1) : sz;
}
