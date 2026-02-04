#include "message_view.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QRadioButton>
#include <QSplitter>
#include <QVBoxLayout>

#include "core/commands/commands.h"
#include "mainwin.h"
#include "message_edit.h"
#include "message_inspector.h"
#include "modules/system/stream_manager.h"

MessageView::MessageView(ChartsPanel* charts, QWidget* parent) : charts(charts), QWidget(parent) {
  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);

  // 1. Navigation & Controls
  tabbar = new TabBar(this);
  tabbar->setUsesScrollButtons(true);
  tabbar->setAutoHide(true);
  tabbar->setContextMenuPolicy(Qt::CustomContextMenu);

  main_layout->addWidget(tabbar);
  main_layout->addWidget(createToolBar());

  // 2. Warning Bar (Simplified)
  warning_widget = new QWidget(this);
  auto* warning_h = new QHBoxLayout(warning_widget);
  warning_h->setContentsMargins(8, 4, 8, 4);
  warning_h->addWidget(warning_icon = new QLabel(this), 0, Qt::AlignTop);
  warning_h->addWidget(warning_label = new QLabel(this), 1);
  warning_widget->hide();
  main_layout->addWidget(warning_widget);

  // 3. Main Content (Splitter)
  splitter = new PanelSplitter(Qt::Vertical, this);
  splitter->setChildrenCollapsible(true);
  splitter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  binary_view = new BinaryView(this);
  signal_editor = new SignalEditor(charts, this);
  message_history = new MessageHistory(this);

  binary_model = new BinaryModel(this);
  binary_view->setModel(binary_model);

  tab_widget = new QTabWidget(this);
  tab_widget->setTabPosition(QTabWidget::South);
  tab_widget->addTab(signal_editor, utils::icon("list-tree"), tr("&Signals"));
  tab_widget->addTab(message_history, utils::icon("history"), tr("&Trace"));
  tab_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  splitter->addWidget(binary_view);
  splitter->addWidget(tab_widget);

  // Stretch factor: BinaryView stays small (0), Tabs expand to fill space (1)
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);

  main_layout->addWidget(splitter);

  updateOrientationButton();
  setupConnections();
}

void MessageView::setupConnections() {
  connect(binary_view, &BinaryView::signalHovered, signal_editor, &SignalEditor::signalHovered);
  connect(binary_view, &BinaryView::signalClicked, [this](const dbc::Signal* s) { signal_editor->selectSignal(s, true); });
  connect(binary_view, &BinaryView::editSignal, signal_editor->model, &SignalTreeModel::saveSignal);
  connect(binary_view, &BinaryView::showChart, charts, &ChartsPanel::showChart);
  connect(signal_editor, &SignalEditor::showChart, charts, &ChartsPanel::showChart);
  connect(signal_editor, &SignalEditor::highlight, binary_view, &BinaryView::highlight);
  connect(tab_widget, &QTabWidget::currentChanged, [this]() { updateState(); });
  connect(&StreamManager::instance(), &StreamManager::snapshotsUpdated, this, &MessageView::updateState);
  connect(GetDBC(), &dbc::Manager::DBCFileChanged, this, &MessageView::refresh);
  connect(UndoStack::instance(), &QUndoStack::indexChanged, this, &MessageView::refresh);
  connect(tabbar, &QTabBar::customContextMenuRequested, this, &MessageView::showTabBarContextMenu);
  connect(tabbar, &QTabBar::currentChanged, [this](int index) {
    if (index != -1) {
      MessageId id = tabbar->tabData(index).value<MessageId>();
      setMessage(id);
      emit activeMessageChanged(id);
    } else {
      emit activeMessageChanged(MessageId());
    }
  });
  connect(tabbar, &QTabBar::tabCloseRequested, tabbar, &QTabBar::removeTab);
}

QWidget* MessageView::createToolBar() {
  QWidget* toolbar_container = new QWidget(this);
  QHBoxLayout* hl = new QHBoxLayout(toolbar_container);

  int spacing = style()->pixelMetric(QStyle::PM_ToolBarItemSpacing);
  hl->setContentsMargins(4, 2, 4, 2);  // Tight vertical padding
  hl->setSpacing(spacing);

  // Name Label
  hl->addWidget(name_label = new ElidedLabel(this));
  name_label->setStyleSheet("font-weight:bold;");

  hl->addStretch();

  // Heatmap Controls
  QComboBox* heatmap_mode = new QComboBox(this);
  heatmap_mode->setFocusPolicy(Qt::NoFocus);
  heatmap_mode->addItem(tr("Live Heatmap"), true);
  heatmap_mode->addItem(tr("Timeline Heatmap"), false);
  QString live_desc = tr("• <b>Live:</b> Total flips since last seek or stream start.");
  QString time_desc = tr("• <b>Timeline:</b> Bit flips within the current zoom or full range.");
  heatmap_mode->setToolTip(tr("<b>Heatmap Mode</b><br/>%1<br/>%2").arg(live_desc, time_desc));

  connect(heatmap_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, heatmap_mode](int index) {
    binary_model->setHeatmapMode(heatmap_mode->itemData(index).toBool());
  });
  hl->addWidget(heatmap_mode);

  // Layout Orientation Toggle
  toggle_orientation_btn_ = new ToolButton();
  hl->addWidget(toggle_orientation_btn_);
  connect(toggle_orientation_btn_, &ToolButton::clicked, this, &MessageView::toggleCenterOrientation);

  // Action Buttons
  ToolButton* edit_btn = new ToolButton("square-pen", tr("Edit Message"));
  connect(edit_btn, &ToolButton::clicked, this, &MessageView::editMsg);
  hl->addWidget(edit_btn);

  hl->addSpacing(4);
  hl->addWidget(createVLine(this), 0, Qt::AlignCenter);
  hl->addSpacing(4);
  remove_msg_btn = new ToolButton("trash-2", tr("Remove Message"));
  remove_msg_btn->setHoverColor(QColor(220, 53, 69));
  connect(remove_msg_btn, &ToolButton::clicked, this, &MessageView::removeMsg);
  hl->addWidget(remove_msg_btn);
  return toolbar_container;
}

void MessageView::showTabBarContextMenu(const QPoint& pt) {
  int index = tabbar->tabAt(pt);
  if (index >= 0) {
    QMenu menu(this);
    menu.addAction(tr("Close Other Tabs"));
    if (menu.exec(tabbar->mapToGlobal(pt))) {
      tabbar->moveTab(index, 0);
      tabbar->setCurrentIndex(0);
      while (tabbar->count() > 1) {
        tabbar->removeTab(1);
      }
    }
  }
}

int MessageView::findOrAddTab(const MessageId& id) {
  for (int i = 0; i < tabbar->count(); ++i) {
    if (tabbar->tabData(i).value<MessageId>() == id) return i;
  }
  int index = tabbar->addTab(id.toString());
  tabbar->setTabData(index, QVariant::fromValue(id));
  tabbar->setTabToolTip(index, msgName(id));
  return index;
}

void MessageView::setMessage(const MessageId& message_id) {
  if (std::exchange(msg_id, message_id) == message_id) return;

  tabbar->blockSignals(true);
  int index = findOrAddTab(message_id);
  tabbar->setCurrentIndex(index);
  tabbar->blockSignals(false);

  setUpdatesEnabled(false);
  signal_editor->setMessage(msg_id);
  binary_model->setMessage(msg_id);
  message_history->setMessage(msg_id);
  refresh();
  setUpdatesEnabled(true);
}

void MessageView::resetState() {
  msg_id = MessageId();
  tabbar->blockSignals(true);
  for (int i = tabbar->count() - 1; i >= 0; --i) {
    tabbar->removeTab(i);
  }
  tabbar->blockSignals(false);
  binary_model->setMessage({});
  signal_editor->clearMessage();
  message_history->clearMessage();
}

std::pair<QString, QStringList> MessageView::serializeMessageIds() const {
  QStringList msgs;
  for (int i = 0; i < tabbar->count(); ++i) {
    MessageId id = tabbar->tabData(i).value<MessageId>();
    msgs.append(id.toString());
  }
  return std::make_pair(msg_id.toString(), msgs);
}

void MessageView::restoreTabs(const QString active_msg_id, const QStringList& msg_ids) {
  resetState();
  tabbar->blockSignals(true);
  for (const auto& str_id : msg_ids) {
    MessageId id = MessageId::fromString(str_id);
    if (GetDBC()->msg(id) != nullptr)
      findOrAddTab(id);
  }
  tabbar->blockSignals(false);

  auto active_id = MessageId::fromString(active_msg_id);
  if (GetDBC()->msg(active_id) != nullptr) {
    setMessage(active_id);
  } else if (tabbar->count() > 0) {
    tabbar->setCurrentIndex(0);
  }
}

void MessageView::refresh() {
  QStringList warnings;
  auto msg = GetDBC()->msg(msg_id);
  auto* can_msg = StreamManager::stream()->snapshot(msg_id);
  if (msg) {
    if (msg_id.source == INVALID_SOURCE) {
      warnings.push_back(tr("No messages received."));
    } else if (can_msg->ts > 0 && msg->size != can_msg->size) {
      warnings.push_back(tr("Message size (%1) is incorrect.").arg(msg->size));
    }
    for (auto s : binary_view->getOverlappingSignals()) {
      warnings.push_back(tr("%1 has overlapping bits.").arg(s->name));
    }
  }
  QString msg_name = msg ? QString("%1 (%2)").arg(msg->name, msg->transmitter) : msgName(msg_id);
  name_label->setText(msg_name);
  name_label->setToolTip(msg_name);
  remove_msg_btn->setEnabled(msg != nullptr);

  if (!warnings.isEmpty()) {
    warning_label->setText(warnings.join('\n'));
    warning_icon->setPixmap(utils::icon(msg ? "circle-alert" : "info", {16, 16}));
  }
  warning_widget->setVisible(!warnings.isEmpty());
}

void MessageView::updateState(const std::set<MessageId>* msgs) {
  if ((msgs && !msgs->count(msg_id))) return;

  binary_model->updateState();
  if (tab_widget->currentIndex() == 0) {
    signal_editor->updateState();
  } else {
    message_history->updateState();
  }
}

void MessageView::editMsg() {
  auto msg = GetDBC()->msg(msg_id);
  int size = msg ? msg->size : StreamManager::stream()->snapshot(msg_id)->size;
  MessageEdit dlg(msg_id, msgName(msg_id), size, this);
  if (dlg.exec()) {
    UndoStack::push(new EditMsgCommand(msg_id, dlg.name_edit->text().trimmed(), dlg.size_spin->value(),
                                       dlg.node->text().trimmed(), dlg.comment_edit->toPlainText().trimmed()));
  }
}

void MessageView::removeMsg() {
  UndoStack::push(new RemoveMsgCommand(msg_id));
}

void MessageView::toggleCenterOrientation() {
  bool willBeHorizontal = splitter->orientation() == Qt::Vertical;
  if (willBeHorizontal) {
    splitter->setOrientation(Qt::Horizontal);
    emit requestDockCompression();

    int totalWidth = splitter->width();
    splitter->setSizes({totalWidth / 2, totalWidth / 2});
    toggle_orientation_btn_->setIcon(("rows-2"));
  } else {
    splitter->setOrientation(Qt::Vertical);
    int totalHeight = splitter->height();
    int bin_height = binary_view->sizeHint().height();
    splitter->setSizes({bin_height, totalHeight - bin_height});
    // emit requestDockExpansion();
    toggle_orientation_btn_->setIcon(("columns-2"));
  }
  updateOrientationButton();
}

void MessageView::updateOrientationButton() {
  if (splitter->orientation() == Qt::Vertical) {
    toggle_orientation_btn_->setIcon("columns-2");
    toggle_orientation_btn_->setToolTip(tr("Show side-by-side\nArrange binary and signal views horizontally"));
  } else {
    toggle_orientation_btn_->setIcon("rows-2");
    toggle_orientation_btn_->setToolTip(tr("Stack vertically\nShow binary view above signal view"));
  }
}
