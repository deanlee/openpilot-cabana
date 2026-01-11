#include "message_inspector.h"

#include <QMenu>
#include <QRadioButton>
#include <QToolBar>

#include "core/commands/commands.h"
#include "mainwin.h"
#include "message_edit.h"
#include "modules/system/stream_manager.h"

MessageDetails::MessageDetails(ChartsPanel *charts, QWidget *parent) : charts(charts), QWidget(parent) {
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);

  // tabbar
  tabbar = new TabBar(this);
  tabbar->setUsesScrollButtons(true);
  tabbar->setAutoHide(true);
  tabbar->setContextMenuPolicy(Qt::CustomContextMenu);
  main_layout->addWidget(tabbar);

  createToolBar();

  // warning
  warning_widget = new QWidget(this);
  QHBoxLayout *warning_hlayout = new QHBoxLayout(warning_widget);
  warning_hlayout->addWidget(warning_icon = new QLabel(this), 0, Qt::AlignTop);
  warning_hlayout->addWidget(warning_label = new QLabel(this), 1, Qt::AlignLeft);
  warning_widget->hide();
  main_layout->addWidget(warning_widget);

  // msg widget
  splitter = new QSplitter(Qt::Vertical, this);
  splitter->addWidget(binary_view = new BinaryView(this));
  splitter->addWidget(signal_editor = new SignalEditor(charts, this));
  binary_view->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
  signal_editor->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);

  tab_widget = new QTabWidget(this);
  tab_widget->setStyleSheet("QTabWidget::pane {border: none; margin-bottom: -2px;}");
  tab_widget->setTabPosition(QTabWidget::South);
  tab_widget->addTab(splitter, utils::icon("binary"), "&Msg");
  tab_widget->addTab(message_history = new MessageHistory(this), utils::icon("scroll-text"), "&Logs");
  main_layout->addWidget(tab_widget);

  connect(binary_view, &BinaryView::signalHovered, signal_editor, &SignalEditor::signalHovered);
  connect(binary_view, &BinaryView::signalClicked, [this](const dbc::Signal *s) { signal_editor->selectSignal(s, true); });
  connect(binary_view, &BinaryView::editSignal, signal_editor->model, &SignalTreeModel::saveSignal);
  connect(binary_view, &BinaryView::showChart, charts, &ChartsPanel::showChart);
  connect(signal_editor, &SignalEditor::showChart, charts, &ChartsPanel::showChart);
  connect(signal_editor, &SignalEditor::highlight, binary_view, &BinaryView::highlight);
  connect(tab_widget, &QTabWidget::currentChanged, [this]() { updateState(); });
  connect(&StreamManager::instance(), &StreamManager::snapshotsUpdated, this, &MessageDetails::updateState);
  connect(GetDBC(), &dbc::Manager::DBCFileChanged, this, &MessageDetails::refresh);
  connect(UndoStack::instance(), &QUndoStack::indexChanged, this, &MessageDetails::refresh);
  connect(tabbar, &QTabBar::customContextMenuRequested, this, &MessageDetails::showTabBarContextMenu);
  connect(tabbar, &QTabBar::currentChanged, [this](int index) {
    if (index != -1) {
      setMessage(tabbar->tabData(index).value<MessageId>());
    }
  });
  connect(tabbar, &QTabBar::tabCloseRequested, tabbar, &QTabBar::removeTab);
  connect(charts, &ChartsPanel::seriesChanged, signal_editor, &SignalEditor::updateChartState);
}

void MessageDetails::createToolBar() {
  QToolBar *toolbar = new QToolBar(this);
  int icon_size = style()->pixelMetric(QStyle::PM_SmallIconSize);
  toolbar->setIconSize({icon_size, icon_size});
  toolbar->addWidget(name_label = new ElidedLabel(this));
  name_label->setStyleSheet("QLabel{font-weight:bold;}");

  QWidget *spacer = new QWidget();
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  toolbar->addWidget(spacer);

// Heatmap label and radio buttons
  toolbar->addWidget(new QLabel(tr("Heatmap:"), this));
  auto *heatmap_live = new QRadioButton(tr("Live"), this);
  auto *heatmap_all = new QRadioButton(tr("All"), this);
  heatmap_live->setChecked(true);

  toolbar->addWidget(heatmap_live);
  toolbar->addWidget(heatmap_all);

  // Edit and remove buttons
  toolbar->addSeparator();
  toolbar->addAction(utils::icon("square-pen"), tr("Edit Message"), this, &MessageDetails::editMsg);
  action_remove_msg = toolbar->addAction(utils::icon("trash-2"), tr("Remove Message"), this, &MessageDetails::removeMsg);

  layout()->addWidget(toolbar);

  connect(heatmap_live, &QAbstractButton::toggled, this, [this](bool on) { binary_view->setHeatmapLiveMode(on); });
  connect(&StreamManager::instance(), &StreamManager::timeRangeChanged, this, [=](const std::optional<std::pair<double, double>> &range) {
    auto text = range ? QString("%1 - %2").arg(range->first, 0, 'f', 3).arg(range->second, 0, 'f', 3) : "All";
    heatmap_all->setText(text);
    (range ? heatmap_all : heatmap_live)->setChecked(true);
  });
}

void MessageDetails::showTabBarContextMenu(const QPoint &pt) {
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

int MessageDetails::findOrAddTab(const MessageId& id) {
  for (int i = 0; i < tabbar->count(); ++i) {
    if (tabbar->tabData(i).value<MessageId>() == id) return i;
  }
  int index = tabbar->addTab(id.toString());
  tabbar->setTabData(index, QVariant::fromValue(id));
  tabbar->setTabToolTip(index, msgName(id));
  return index;
}

void MessageDetails::setMessage(const MessageId &message_id) {
  if (std::exchange(msg_id, message_id) == message_id) return;

  tabbar->blockSignals(true);
  int index = findOrAddTab(message_id);
  tabbar->setCurrentIndex(index);
  tabbar->blockSignals(false);

  setUpdatesEnabled(false);
  signal_editor->setMessage(msg_id);
  binary_view->setMessage(msg_id);
  message_history->setMessage(msg_id);
  refresh();
  setUpdatesEnabled(true);
}

void MessageDetails::resetState() {
  // tabbar->clear();
  msg_id = MessageId();
  tabbar->blockSignals(true);
  for (int i = tabbar->count() - 1; i >= 0; --i) {
    tabbar->removeTab(i);
  }
  tabbar->blockSignals(false);
  binary_view->clearMessage();
  signal_editor->clearMessage();
  message_history->clearMessage();
}

std::pair<QString, QStringList> MessageDetails::serializeMessageIds() const {
  QStringList msgs;
  for (int i = 0; i < tabbar->count(); ++i) {
    MessageId id = tabbar->tabData(i).value<MessageId>();
    msgs.append(id.toString());
  }
  return std::make_pair(msg_id.toString(), msgs);
}

void MessageDetails::restoreTabs(const QString active_msg_id, const QStringList& msg_ids) {
  tabbar->blockSignals(true);
  for (const auto& str_id : msg_ids) {
    MessageId id = MessageId::fromString(str_id);
    if (GetDBC()->msg(id) != nullptr)
      findOrAddTab(id);
  }
  tabbar->blockSignals(false);

  auto active_id = MessageId::fromString(active_msg_id);
  if (GetDBC()->msg(active_id) != nullptr)
    setMessage(active_id);
}

void MessageDetails::refresh() {
  QStringList warnings;
  auto msg = GetDBC()->msg(msg_id);
  auto *can_msg = StreamManager::stream()->snapshot(msg_id);
  if (msg) {
    if (msg_id.source == INVALID_SOURCE) {
      warnings.push_back(tr("No messages received."));
    } else if (can_msg->ts > 0 && msg->size != can_msg->dat.size()) {
      warnings.push_back(tr("Message size (%1) is incorrect.").arg(msg->size));
    }
    for (auto s : binary_view->getOverlappingSignals()) {
      warnings.push_back(tr("%1 has overlapping bits.").arg(s->name));
    }
  }
  QString msg_name = msg ? QString("%1 (%2)").arg(msg->name, msg->transmitter) : msgName(msg_id);
  name_label->setText(msg_name);
  name_label->setToolTip(msg_name);
  action_remove_msg->setEnabled(msg != nullptr);

  if (!warnings.isEmpty()) {
    warning_label->setText(warnings.join('\n'));
    warning_icon->setPixmap(utils::icon(msg ? "triangle-alert" : "info"));
  }
  warning_widget->setVisible(!warnings.isEmpty());
}

void MessageDetails::updateState(const std::set<MessageId> *msgs) {
  if ((msgs && !msgs->count(msg_id)))
    return;

  if (tab_widget->currentIndex() == 0)
    binary_view->updateState();
  else
    message_history->updateState();
}

void MessageDetails::editMsg() {
  auto msg = GetDBC()->msg(msg_id);
  int size = msg ? msg->size : StreamManager::stream()->snapshot(msg_id)->dat.size();
  MessageEdit dlg(msg_id, msgName(msg_id), size, this);
  if (dlg.exec()) {
    UndoStack::push(new EditMsgCommand(msg_id, dlg.name_edit->text().trimmed(), dlg.size_spin->value(),
                                       dlg.node->text().trimmed(), dlg.comment_edit->toPlainText().trimmed()));
  }
  }

void MessageDetails::removeMsg() {
  UndoStack::push(new RemoveMsgCommand(msg_id));
}

// CenterWidget

CenterWidget::CenterWidget(QWidget* parent) : QStackedWidget(parent) {
  addWidget(welcome_widget = createWelcomeWidget());
  addWidget(details = new MessageDetails(((MainWindow*)parentWidget())->charts_widget, this));
}

void CenterWidget::setMessage(const MessageId& message_id) {
  if (currentWidget() != details) {
    setCurrentWidget(details);
  }
  details->setMessage(message_id);
}

void CenterWidget::clear() {
  details->resetState();
  if (currentWidget() != welcome_widget) {
    setCurrentWidget(welcome_widget);
  }
}

QWidget* CenterWidget::createWelcomeWidget() {
  QWidget* w = new QWidget(this);
  QVBoxLayout* main_layout = new QVBoxLayout(w);
  main_layout->addStretch(0);
  QLabel* logo = new QLabel("CABANA");
  logo->setAlignment(Qt::AlignCenter);
  logo->setStyleSheet("font-size:50px;font-weight:bold;");
  main_layout->addWidget(logo);

  auto newShortcutRow = [](const QString& title, const QString& key) {
    QHBoxLayout* hlayout = new QHBoxLayout();
    auto btn = new QToolButton();
    btn->setText(key);
    btn->setEnabled(false);
    hlayout->addWidget(new QLabel(title), 0, Qt::AlignRight);
    hlayout->addWidget(btn, 0, Qt::AlignLeft);
    return hlayout;
  };

  auto lb = new QLabel(tr("<-Select a message to view details"));
  lb->setAlignment(Qt::AlignHCenter);
  main_layout->addWidget(lb);
  main_layout->addLayout(newShortcutRow("Pause", "Space"));
  main_layout->addLayout(newShortcutRow("Help", "F1"));
  main_layout->addLayout(newShortcutRow("WhatsThis", "Shift+F1"));
  main_layout->addStretch(0);

  w->setStyleSheet("QLabel{color:darkGray;}");
  w->setBackgroundRole(QPalette::Base);
  w->setAutoFillBackground(true);
  return w;
}
