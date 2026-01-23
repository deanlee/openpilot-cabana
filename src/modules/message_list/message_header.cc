#include "message_header.h"

#include "message_model.h"

MessageHeader::MessageHeader(QWidget* parent) : QHeaderView(Qt::Horizontal, parent) {
  connect(this, &QHeaderView::sectionResized, this, &MessageHeader::updateHeaderPositions);
  connect(this, &QHeaderView::sectionMoved, this, &MessageHeader::updateHeaderPositions);
}

MessageHeader::~MessageHeader() {
  // Clear the map; Qt handles child widget deletion automatically
  // but clearing the map prevents slots from firing on dead pointers.
  editors.clear();
}

void MessageHeader::setModel(QAbstractItemModel* model) {
  if (this->model()) {
    disconnect(this->model(), nullptr, this, nullptr);
  }
  QHeaderView::setModel(model);
  if (model) {
    // Wipe editors when the model changes (e.g., loading a new DBC)
    connect(model, &QAbstractItemModel::modelReset, this, [this]() { updateGeometries(); });
  }
}

void MessageHeader::clearEditors() {
  for (auto edit : editors) {
    if (edit) edit->deleteLater();
  }
  editors.clear();
}

void MessageHeader::updateFilters() {
  auto m = qobject_cast<MessageModel*>(model());
  if (!m) return;

  QMap<int, QString> filters;
  for (auto it = editors.begin(); it != editors.end(); ++it) {
    if (it.value() && !it.value()->text().isEmpty()) {
      filters[it.key()] = it.value()->text();
    }
  }
  m->setFilterStrings(filters);
}

void MessageHeader::updateGeometries() {
  if (is_updating || !model() || count() <= 0) {
    QHeaderView::updateGeometries();
    return;
  }

  is_updating = true;

  // 1. Sync Editors with Column Count
  for (int i = 0; i < count(); ++i) {
    if (!editors.contains(i)) {
      QString col_name = model()->headerData(i, Qt::Horizontal).toString();
      auto* edit = new DebouncedLineEdit(this);
      edit->setClearButtonEnabled(true);
      edit->setPlaceholderText(tr("Filter %1").arg(col_name));

      QString tooltip;
      if (i == MessageModel::Column::SOURCE || i == MessageModel::Column::ADDRESS ||
          i == MessageModel::Column::FREQ || i == MessageModel::Column::COUNT) {
        tooltip = tr("<b>Range Filter</b><br>"
            "• Single value: <i>10</i><br>"
            "• Range: <i>10-20</i><br>"
            "• Minimum: <i>10-</i><br>"
            "• Maximum: <i>-20</i>");

        if (i == MessageModel::Column::ADDRESS) {
          tooltip += tr("<br><span style='color:gray;'>Values in Hexadecimal</span>");
        }
      } else if (i == MessageModel::Column::DATA) {
        tooltip = tr("Filter by hex byte");
      } else {
        tooltip = tr("Filter by name");
      }

      edit->setToolTip(tooltip);

      // Connect with 'this' context for safety
      connect(edit, &DebouncedLineEdit::debouncedTextEdited, this, [this]() { updateFilters(); });
      editors[i] = edit;
    }
  }

  // 2. Recursion Guard for Margins
  int required_h = editors[0]->sizeHint().height();
  if (viewportMargins().bottom() != required_h) {
    cached_editor_height = required_h;
    setViewportMargins(0, 0, 0, required_h);
  }

  QHeaderView::updateGeometries();
  updateHeaderPositions();
  is_updating = false;
}

void MessageHeader::updateHeaderPositions() {
  if (editors.isEmpty()) return;

  int header_h = QHeaderView::sizeHint().height();
  for (int i = 0; i < count(); ++i) {
    auto edit = editors.value(i);
    if (edit) {
      edit->setGeometry(sectionViewportPosition(i), header_h, sectionSize(i), edit->sizeHint().height());
      edit->setHidden(isSectionHidden(i));
    }
  }
}

QSize MessageHeader::sizeHint() const {
  QSize sz = QHeaderView::sizeHint();
  if (cached_editor_height > 0) {
    sz.setHeight(sz.height() + cached_editor_height + 1);
  } else if (auto first = editors.value(0)) {
    sz.setHeight(sz.height() + first->sizeHint().height() + 1);
  }
  return sz;
}
