#include "message_history.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>

#include "core/commands/commands.h"
#include "modules/dbc/export.h"
#include "modules/settings/settings.h"
#include "modules/system/stream_manager.h"
#include "widgets/validators.h"

MessageHistory::MessageHistory(QWidget* parent) : QFrame(parent) {
  setFrameStyle(QFrame::NoFrame);

  QVBoxLayout* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(0);

  // 1. Initialize logic and views
  model = new MessageHistoryModel(this);
  delegate = new MessageDelegate(this, CallerType::HistoryView);
  logs = new HistoryTableView(this);

  // 2. Setup UI Components
  main_layout->addWidget(createToolbar());

  QFrame* line = new QFrame(this);
  line->setFrameStyle(QFrame::HLine | QFrame::Sunken);
  main_layout->addWidget(line);

  // 3. Configure Table
  logs->setModel(model);
  logs->setItemDelegate(delegate);
  logs->setHorizontalHeader(new HistoryHeader(Qt::Horizontal, this));
  logs->setFrameShape(QFrame::NoFrame);

  auto* header = logs->horizontalHeader();
  header->setResizeContentsPrecision(100);
  header->setHighlightSections(false);

  logs->verticalHeader()->setDefaultSectionSize(delegate->sizeForBytes(8).height());
  logs->verticalHeader()->setVisible(false);

  main_layout->addWidget(logs);

  setupConnections();
}

QWidget* MessageHistory::createToolbar() {
  QWidget* toolbar = new QWidget(this);
  QHBoxLayout* h = new QHBoxLayout(toolbar);
  h->setContentsMargins(4, 4, 4, 4);

  // Filter group
  filters_widget = new QWidget(this);
  QHBoxLayout* filter_layout = new QHBoxLayout(filters_widget);
  filter_layout->setContentsMargins(0, 0, 0, 0);

  filter_layout->addWidget(display_type_cb = new QComboBox(this));
  filter_layout->addWidget(signals_cb = new QComboBox(this));
  filter_layout->addWidget(comp_box = new QComboBox(this));
  filter_layout->addWidget(value_edit = new DebouncedLineEdit(this));

  // Configure widgets
  display_type_cb->addItems({tr("Signal"), tr("Hex")});
  comp_box->addItems({">", "=", "!=", "<"});

  value_edit->setPlaceholderText(tr("Filter value..."));
  value_edit->setClearButtonEnabled(true);
  value_edit->setValidator(new DoubleValidator(this));

  signals_cb->setToolTip(tr("Select a signal to filter by its value"));
  comp_box->setToolTip(tr("Comparison operator for filtering"));
  display_type_cb->setToolTip(tr("Display signal value or raw hex value"));

  // Toolbar assembly
  h->addWidget(filters_widget);
  h->addStretch(1);

  export_btn = new ToolButton("file-spreadsheet", tr("Export to CSV file..."));
  h->addWidget(export_btn);

  return toolbar;
}

void MessageHistory::setupConnections() {
  connect(display_type_cb, qOverload<int>(&QComboBox::activated), this, &MessageHistory::handleDisplayTypeChange);
  connect(signals_cb, SIGNAL(activated(int)), this, SLOT(filterChanged()));
  connect(comp_box, SIGNAL(activated(int)), this, SLOT(filterChanged()));
  connect(value_edit, &DebouncedLineEdit::debouncedTextEdited, this, &MessageHistory::filterChanged);
  connect(export_btn, &QToolButton::clicked, this, &MessageHistory::exportToCSV);
  connect(&StreamManager::instance(), &StreamManager::seekedTo, model, &MessageHistoryModel::reset);
  connect(&StreamManager::instance(), &StreamManager::paused, model, &MessageHistoryModel::setPaused);
  connect(&StreamManager::instance(), &StreamManager::resume, model, &MessageHistoryModel::setResumed);
  connect(GetDBC(), &dbc::Manager::DBCFileChanged, model, &MessageHistoryModel::reset);
  connect(UndoStack::instance(), &QUndoStack::indexChanged, model, &MessageHistoryModel::reset);
  connect(model, &MessageHistoryModel::modelReset, this, &MessageHistory::modelReset);
  connect(model, &MessageHistoryModel::rowsInserted, [this]() { export_btn->setEnabled(true); });
}

void MessageHistory::handleDisplayTypeChange(int index) {
  model->setHexMode(index);
}

void MessageHistory::clearMessage() {
  model->setMessage(MessageId());
}

void MessageHistory::modelReset() {
  signals_cb->clear();
  for (auto &s : model->sigs) {
    signals_cb->addItem(s.sig->name);
  }
  export_btn->setEnabled(false);
  value_edit->clear();
  comp_box->setCurrentIndex(0);
  filters_widget->setVisible(!model->sigs.empty());
}

void MessageHistory::filterChanged() {
  if (value_edit->text().isEmpty() && !value_edit->isModified()) return;

  std::function<bool(double, double)> cmp = nullptr;
  switch (comp_box->currentIndex()) {
    case 0: cmp = std::greater<double>{}; break;
    case 1: cmp = std::equal_to<double>{}; break;
    case 2: cmp = [](double l, double r) { return l != r; }; break; // not equal
    case 3: cmp = std::less<double>{}; break;
  }
  model->setFilter(signals_cb->currentIndex(), value_edit->text(), cmp);
}

void MessageHistory::exportToCSV() {
  QString dir = QString("%1/%2_%3.csv").arg(settings.last_dir).arg(StreamManager::stream()->routeName()).arg(msgName(model->msg_id));
  QString fn = QFileDialog::getSaveFileName(this, QString("Export %1 to CSV file").arg(msgName(model->msg_id)),
                                            dir, tr("csv (*.csv)"));
  if (!fn.isEmpty()) {
    model->isHexMode() ? exportMessagesToCSV(fn, model->msg_id)
                       : exportSignalsToCSV(fn, model->msg_id);
    QMessageBox::information(this, tr("Export Success"),
                             tr("Successfully exported to:\n%1").arg(fn));
  }
}
