#include "messageswidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

#include "commands.h"
#include "common_widgets.h"
#include "settings.h"

MessagesWidget::MessagesWidget(QWidget *parent) : menu(new QMenu(this)), QWidget(parent) {
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  // toolbar
  main_layout->addWidget(createToolBar());
  // message table
  main_layout->addWidget(view = new MessageView(this));
  view->setItemDelegate(delegate = new MessageTableDelegate(view, settings.multiple_lines_hex));
  view->setModel(model = new MessageTableModel(this));
  view->setHeader(header = new MessageViewHeader(this));
  view->setSortingEnabled(true);
  view->sortByColumn(MessageTableModel::Column::NAME, Qt::AscendingOrder);
  view->setAllColumnsShowFocus(true);
  view->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view->setItemsExpandable(false);
  view->setIndentation(0);
  view->setRootIsDecorated(false);

  // Must be called before setting any header parameters to avoid overriding
  restoreHeaderState(settings.message_header_state);
  header->setSectionsMovable(true);
  header->setSectionResizeMode(MessageTableModel::Column::DATA, QHeaderView::Fixed);
  header->setStretchLastSection(true);
  header->setContextMenuPolicy(Qt::CustomContextMenu);

  // signals/slots
  connect(menu, &QMenu::aboutToShow, this, &MessagesWidget::menuAboutToShow);
  connect(header, &MessageViewHeader::customContextMenuRequested, this, &MessagesWidget::headerContextMenuEvent);
  connect(view->horizontalScrollBar(), &QScrollBar::valueChanged, header, &MessageViewHeader::updateHeaderPositions);
  connect(can, &AbstractStream::snapshotsUpdated, model, &MessageTableModel::onSnapshotsUpdated);
  connect(dbc(), &DBCManager::DBCFileChanged, model, &MessageTableModel::dbcModified);
  connect(UndoStack::instance(), &QUndoStack::indexChanged, model, &MessageTableModel::dbcModified);
  connect(model, &MessageTableModel::modelReset, [this]() {
    if (current_msg_id) {
      selectMessage(*current_msg_id);
    }
    view->updateBytesSectionSize();
    updateTitle();
  });
  connect(view->selectionModel(), &QItemSelectionModel::currentChanged, [=](const QModelIndex &current, const QModelIndex &previous) {
    if (current.isValid() && current.row() < model->items_.size()) {
      const auto &id = model->items_[current.row()].id;
      if (!current_msg_id || id != *current_msg_id) {
        current_msg_id = id;
        emit msgSelectionChanged(*current_msg_id);
      }
    }
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

QWidget *MessagesWidget::createToolBar() {
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

  connect(suppress_add, &QPushButton::clicked, this, &MessagesWidget::suppressHighlighted);
  connect(suppress_clear, &QPushButton::clicked, this, &MessagesWidget::suppressHighlighted);
  connect(suppress_defined_signals, &QCheckBox::stateChanged, can, &AbstractStream::suppressDefinedSignals);

  suppressHighlighted();
  return toolbar;
}

void MessagesWidget::updateTitle() {
  auto stats = std::accumulate(
      model->items_.begin(), model->items_.end(), std::pair<size_t, size_t>(),
      [](const auto &pair, const auto &item) {
        auto m = dbc()->msg(item.id);
        return m ? std::make_pair(pair.first + 1, pair.second + m->sigs.size()) : pair;
      });
  emit titleChanged(tr("%1 Messages (%2 DBC Messages, %3 Signals)")
                      .arg(model->items_.size()).arg(stats.first).arg(stats.second));
}

void MessagesWidget::selectMessage(const MessageId &msg_id) {
  auto it = std::find_if(model->items_.cbegin(), model->items_.cend(),
                         [&msg_id](auto &item) { return item.id == msg_id; });
  if (it != model->items_.cend()) {
    view->setCurrentIndex(model->index(std::distance(model->items_.cbegin(), it), 0));
  }
}

void MessagesWidget::suppressHighlighted() {
  int n = sender() == suppress_add ? can->suppressHighlighted() : (can->clearSuppressed(), 0);
  suppress_clear->setText(n > 0 ? tr("Clear (%1)").arg(n) : tr("Clear"));
  suppress_clear->setEnabled(n > 0);
}

void MessagesWidget::headerContextMenuEvent(const QPoint &pos) {
  menu->exec(header->mapToGlobal(pos));
}

void MessagesWidget::menuAboutToShow() {
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
  auto action = menu->addAction(tr("Multi-Line bytes"), this, &MessagesWidget::setMultiLineBytes);
  action->setCheckable(true);
  action->setChecked(settings.multiple_lines_hex);

  action = menu->addAction(tr("Show inactive Messages"), model, &MessageTableModel::showInactivemessages);
  action->setCheckable(true);
  action->setChecked(model->show_inactive_messages);
}

void MessagesWidget::setMultiLineBytes(bool multi) {
  settings.multiple_lines_hex = multi;
  delegate->setMultipleLines(multi);
  view->updateBytesSectionSize();
  view->doItemsLayout();
}

// MessageView

void MessageView::drawRow(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
  QTreeView::drawRow(painter, option, index);

  QPen oldPen = painter->pen();
  static QColor gridColor = QColor::fromRgba(static_cast<QRgb>(style()->styleHint(QStyle::SH_Table_GridLineColor, &option, this)));
  painter->setPen(gridColor);
  // Draw bottom border for the row
  painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
  // Draw vertical borders for each column
  for (int i = 0; i < header()->count(); ++i) {
    int sectionX = header()->sectionViewportPosition(i);
    painter->drawLine(sectionX, option.rect.top(), sectionX, option.rect.bottom());
  }
  painter->setPen(oldPen);
}

void MessageView::dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles) {
  // Bypass the slow call to QTreeView::dataChanged.
  // QTreeView::dataChanged will invalidate the height cache and that's what we don't need in MessageView.
  QAbstractItemView::dataChanged(topLeft, bottomRight, roles);
}

void MessageView::updateBytesSectionSize() {
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

void MessageView::wheelEvent(QWheelEvent *event) {
  if (event->modifiers() == Qt::ShiftModifier) {
    QApplication::sendEvent(horizontalScrollBar(), event);
  } else {
    QTreeView::wheelEvent(event);
  }
}

// MessageViewHeader

MessageViewHeader::MessageViewHeader(QWidget *parent) : QHeaderView(Qt::Horizontal, parent) {
  filter_timer.setSingleShot(true);
  filter_timer.setInterval(300);
  connect(&filter_timer, &QTimer::timeout, this, &MessageViewHeader::updateFilters);
  connect(this, &QHeaderView::sectionResized, this, &MessageViewHeader::updateHeaderPositions);
  connect(this, &QHeaderView::sectionMoved, this, &MessageViewHeader::updateHeaderPositions);
}

void MessageViewHeader::updateFilters() {
  QMap<int, QString> filters;
  for (int i = 0; i < count(); i++) {
    if (editors[i] && !editors[i]->text().isEmpty()) {
      filters[i] = editors[i]->text();
    }
  }
  qobject_cast<MessageTableModel*>(model())->setFilterStrings(filters);
}

void MessageViewHeader::updateHeaderPositions() {
  QSize sz = QHeaderView::sizeHint();
  for (int i = 0; i < count(); i++) {
    if (editors[i]) {
      int h = editors[i]->sizeHint().height();
      editors[i]->setGeometry(sectionViewportPosition(i), sz.height(), sectionSize(i), h);
      editors[i]->setHidden(isSectionHidden(i));
    }
  }
}

void MessageViewHeader::updateGeometries() {
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

QSize MessageViewHeader::sizeHint() const {
  QSize sz = QHeaderView::sizeHint();
  return editors[0] ? QSize(sz.width(), sz.height() + editors[0]->height() + 1) : sz;
}
