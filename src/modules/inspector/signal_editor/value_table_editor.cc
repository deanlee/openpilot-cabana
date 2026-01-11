#include "value_table_editor.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidgetItem>

#include "utils/util.h"
#include "widgets/validators.h"

ValueTableEditor::ValueTableEditor(const ValueTable& descriptions, QWidget* parent) : QDialog(parent) {
  QHBoxLayout* toolbar_layout = new QHBoxLayout();
  add_btn = new QPushButton(utils::icon("plus"), "");
  remove_btn = new QPushButton(utils::icon("minus"), "");
  remove_btn->setEnabled(false);
  toolbar_layout->addWidget(add_btn);
  toolbar_layout->addWidget(remove_btn);
  toolbar_layout->addStretch(0);

  table = new QTableWidget(descriptions.size(), 2, this);
  table->setItemDelegate(new Delegate(this));
  table->setHorizontalHeaderLabels({"Value", "Description"});
  table->horizontalHeader()->setStretchLastSection(true);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  int row = 0;
  for (auto& [val, desc] : descriptions) {
    table->setItem(row, 0, new QTableWidgetItem(QString::number(val)));
    table->setItem(row, 1, new QTableWidgetItem(desc));
    ++row;
  }

  btn_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QVBoxLayout* main_layout = new QVBoxLayout(this);
  main_layout->addLayout(toolbar_layout);
  main_layout->addWidget(table);
  main_layout->addWidget(btn_box);
  setMinimumWidth(500);

  setupConnections();
}

void ValueTableEditor::setupConnections() {
  connect(btn_box, &QDialogButtonBox::accepted, this, &ValueTableEditor::save);
  connect(btn_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(add_btn, &QPushButton::clicked, [this]() {
    table->setRowCount(table->rowCount() + 1);
    table->setItem(table->rowCount() - 1, 0, new QTableWidgetItem);
    table->setItem(table->rowCount() - 1, 1, new QTableWidgetItem);
  });
  connect(remove_btn, &QPushButton::clicked, [this]() { table->removeRow(table->currentRow()); });
  connect(table, &QTableWidget::itemSelectionChanged, [=]() {
    remove_btn->setEnabled(table->currentRow() != -1);
  });
}

void ValueTableEditor::save() {
  for (int i = 0; i < table->rowCount(); ++i) {
    QString val = table->item(i, 0)->text().trimmed();
    QString desc = table->item(i, 1)->text().trimmed();
    if (!val.isEmpty() && !desc.isEmpty()) {
      value_table.push_back({val.toDouble(), desc});
    }
  }
  QDialog::accept();
}

QWidget* ValueTableEditor::Delegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const {
  QLineEdit* edit = new QLineEdit(parent);
  edit->setFrame(false);
  if (index.column() == 0) {
    edit->setValidator(new DoubleValidator(parent));
  }
  return edit;
}
