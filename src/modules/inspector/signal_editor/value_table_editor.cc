#include "value_table_editor.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidgetItem>

#include "utils/util.h"

ValueTableEditor::ValueTableEditor(const ValueTable& descriptions, QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Value Table Editor"));
  setMinimumSize(500, 400);

  QVBoxLayout* main_layout = new QVBoxLayout(this);

  // Toolbar
  QHBoxLayout* toolbar_layout = new QHBoxLayout();
  add_btn = new QPushButton(utils::icon("plus"), tr(" Add Row"));
  remove_btn = new QPushButton(utils::icon("minus"), tr(" Remove Row"));
  remove_btn->setEnabled(false);

  toolbar_layout->addWidget(add_btn);
  toolbar_layout->addWidget(remove_btn);
  toolbar_layout->addStretch();
  main_layout->addLayout(toolbar_layout);

  // Table Setup
  table = new QTableWidget(0, 2, this);
  table->setItemDelegate(new Delegate(this));
  table->setHorizontalHeaderLabels({tr("Raw Value"), tr("Description")});

  table->verticalHeader()->setVisible(false);  // Clean look
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setStretchLastSection(true);

  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setAlternatingRowColors(true);
  table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::AnyKeyPressed);
  table->setTabKeyNavigation(true);  // Smooth tabbing between cells

  // Populate existing data
  for (const auto& it : descriptions) {
    addRow(QString::number((long long)it.first), it.second);
  }

  main_layout->addWidget(table);

  // Dialog Buttons
  btn_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  main_layout->addWidget(btn_box);

  setupConnections();
}

void ValueTableEditor::addRow(const QString& val, const QString& desc) {
  int row = table->rowCount();
  table->insertRow(row);

  QTableWidgetItem* valItem = new QTableWidgetItem(val);
  QTableWidgetItem* descItem = new QTableWidgetItem(desc);

  // Center the numeric value for better legibility
  valItem->setTextAlignment(Qt::AlignCenter);

  table->setItem(row, 0, valItem);
  table->setItem(row, 1, descItem);
}

void ValueTableEditor::setupConnections() {
  connect(btn_box, &QDialogButtonBox::accepted, this, &ValueTableEditor::handleSave);
  connect(btn_box, &QDialogButtonBox::rejected, this, &QDialog::reject);

  // Add Button logic
  connect(add_btn, &QPushButton::clicked, [this]() {
    addRow("", "");
    int lastRow = table->rowCount() - 1;
    table->setCurrentCell(lastRow, 0);
    table->editItem(table->item(lastRow, 0));
  });

  // Remove Button logic
  connect(remove_btn, &QPushButton::clicked, [this]() {
    table->removeRow(table->currentRow());
  });

  connect(table, &QTableWidget::itemSelectionChanged, [this]() {
    remove_btn->setEnabled(table->selectionModel()->hasSelection());
  });

  // Keyboard: Delete key
  QShortcut* deleteShortcut = new QShortcut(QKeySequence::Delete, table);
  connect(deleteShortcut, &QShortcut::activated, [this]() {
    if (table->currentRow() != -1 && !table->isPersistentEditorOpen(table->currentItem())) {
      remove_btn->click();
    }
  });
}

void ValueTableEditor::handleSave() {
  value_table.clear();
  QSet<long long> seen_values;  // Use long long to match DBC raw value logic

  for (int i = 0; i < table->rowCount(); ++i) {
    QTableWidgetItem* valItem = table->item(i, 0);
    QTableWidgetItem* descItem = table->item(i, 1);

    if (!valItem || !descItem) continue;

    QString valStr = valItem->text().trimmed();
    QString desc = descItem->text().trimmed();

    if (valStr.isEmpty() && desc.isEmpty()) continue;  // Skip empty rows

    bool ok;
    long long val = valStr.toLongLong(&ok);
    if (!ok) {
      QMessageBox::warning(this, tr("Invalid Input"), tr("Row %1: Value must be an integer.").arg(i + 1));
      table->setCurrentCell(i, 0);
      return;
    }

    if (seen_values.contains(val)) {
      QMessageBox::warning(this, tr("Duplicate Value"),
                           tr("Value %1 is defined multiple times.").arg(val));
      table->setCurrentCell(i, 0);
      return;
    }

    seen_values.insert(val);
    value_table.push_back({static_cast<double>(val), desc});
  }

  // Final sort to keep DBC file organized
  std::sort(value_table.begin(), value_table.end(),
            [](const std::pair<double, QString>& a, const std::pair<double, QString>& b) {
              return a.first < b.first;
            });

  QDialog::accept();
}

QWidget* ValueTableEditor::Delegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const {
  QLineEdit* edit = new QLineEdit(parent);
  edit->setFrame(false);
  if (index.column() == 0) {
    // Standard DBC raw values are integers
    edit->setValidator(new QIntValidator(parent));
  }
  return edit;
}
