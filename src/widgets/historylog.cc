#include "historylog.h"

#include <QFileDialog>
#include <QPainter>
#include <QVBoxLayout>

#include "commands.h"
#include "utils/export.h"

QSize HeaderView::sectionSizeFromContents(int logicalIndex) const {
  static const QSize time_col_size = fontMetrics().size(Qt::TextSingleLine, "000000.000") + QSize(10, 6);
  if (logicalIndex == 0) {
    return time_col_size;
  } else {
    int default_size = qMax(100, (rect().width() - time_col_size.width()) / (model()->columnCount() - 1));
    QString text = model()->headerData(logicalIndex, this->orientation(), Qt::DisplayRole).toString();
    const QRect rect = fontMetrics().boundingRect({0, 0, default_size, 2000}, defaultAlignment(), text.replace(QChar('_'), ' '));
    QSize size = rect.size() + QSize{10, 6};
    return QSize{qMax(size.width(), default_size), size.height()};
  }
}

void HeaderView::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const {
  auto bg_role = model()->headerData(logicalIndex, Qt::Horizontal, Qt::BackgroundRole);
  if (bg_role.isValid()) {
    painter->fillRect(rect, bg_role.value<QBrush>());
  }
  QString text = model()->headerData(logicalIndex, Qt::Horizontal, Qt::DisplayRole).toString();
  painter->setPen(palette().color(utils::isDarkTheme() ? QPalette::BrightText : QPalette::Text));
  painter->drawText(rect.adjusted(5, 3, -5, -3), defaultAlignment(), text.replace(QChar('_'), ' '));
}

// LogsWidget

LogsWidget::LogsWidget(QWidget *parent) : QFrame(parent) {
  setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(0);

  QWidget *toolbar = new QWidget(this);
  toolbar->setAutoFillBackground(true);
  QHBoxLayout *h = new QHBoxLayout(toolbar);

  filters_widget = new QWidget(this);
  QHBoxLayout *filter_layout = new QHBoxLayout(filters_widget);
  filter_layout->setContentsMargins(0, 0, 0, 0);
  filter_layout->addWidget(display_type_cb = new QComboBox(this));
  filter_layout->addWidget(signals_cb = new QComboBox(this));
  filter_layout->addWidget(comp_box = new QComboBox(this));
  filter_layout->addWidget(value_edit = new QLineEdit(this));
  h->addWidget(filters_widget);
  h->addStretch(0);
  export_btn = new ToolButton("filetype-csv", tr("Export to CSV file..."));
  h->addWidget(export_btn, 0, Qt::AlignRight);

  display_type_cb->addItems({"Signal", "Hex"});
  display_type_cb->setToolTip(tr("Display signal value or raw hex value"));
  comp_box->addItems({">", "=", "!=", "<"});
  value_edit->setClearButtonEnabled(true);
  value_edit->setValidator(new DoubleValidator(this));

  main_layout->addWidget(toolbar);
  QFrame *line = new QFrame(this);
  line->setFrameStyle(QFrame::HLine | QFrame::Sunken);
  main_layout->addWidget(line);
  main_layout->addWidget(logs = new QTableView(this));
  logs->setModel(model = new MessageLogModel(this));
  logs->setItemDelegate(delegate = new MessageBytesDelegate(this));
  logs->setHorizontalHeader(new HeaderView(Qt::Horizontal, this));
  logs->horizontalHeader()->setDefaultAlignment(Qt::AlignRight | (Qt::Alignment)Qt::TextWordWrap);
  logs->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  logs->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  logs->verticalHeader()->setDefaultSectionSize(delegate->sizeForBytes(8).height());
  logs->setFrameShape(QFrame::NoFrame);

  connect(display_type_cb, qOverload<int>(&QComboBox::activated), model, &MessageLogModel::setHexMode);
  connect(signals_cb, SIGNAL(activated(int)), this, SLOT(filterChanged()));
  connect(comp_box, SIGNAL(activated(int)), this, SLOT(filterChanged()));
  connect(value_edit, &QLineEdit::textEdited, this, &LogsWidget::filterChanged);
  connect(export_btn, &QToolButton::clicked, this, &LogsWidget::exportToCSV);
  connect(can, &AbstractStream::seekedTo, model, &MessageLogModel::reset);
  connect(dbc(), &DBCManager::DBCFileChanged, model, &MessageLogModel::reset);
  connect(UndoStack::instance(), &QUndoStack::indexChanged, model, &MessageLogModel::reset);
  connect(model, &MessageLogModel::modelReset, this, &LogsWidget::modelReset);
  connect(model, &MessageLogModel::rowsInserted, [this]() { export_btn->setEnabled(true); });
}

void LogsWidget::modelReset() {
  signals_cb->clear();
  for (auto s : model->sigs) {
    signals_cb->addItem(s->name);
  }
  export_btn->setEnabled(false);
  value_edit->clear();
  comp_box->setCurrentIndex(0);
  filters_widget->setVisible(!model->sigs.empty());
}

void LogsWidget::filterChanged() {
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

void LogsWidget::exportToCSV() {
  QString dir = QString("%1/%2_%3.csv").arg(settings.last_dir).arg(can->routeName()).arg(msgName(model->msg_id));
  QString fn = QFileDialog::getSaveFileName(this, QString("Export %1 to CSV file").arg(msgName(model->msg_id)),
                                            dir, tr("csv (*.csv)"));
  if (!fn.isEmpty()) {
    model->isHexMode() ? utils::exportToCSV(fn, model->msg_id)
                       : utils::exportSignalsToCSV(fn, model->msg_id);
  }
}
