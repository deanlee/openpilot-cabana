#include "message_header.h"

#include "message_model.h"

MessageHeader::MessageHeader(QWidget* parent) : QHeaderView(Qt::Horizontal, parent) {
  connect(this, &QHeaderView::sectionResized, this, &MessageHeader::updateHeaderPositions);
  connect(this, &QHeaderView::sectionMoved, this, &MessageHeader::updateHeaderPositions);
}

void MessageHeader::setModel(QAbstractItemModel* model) {
  if (this->model()) {
    disconnect(this->model(), nullptr, this, nullptr);
  }
  QHeaderView::setModel(model);
  if (model) {
    connect(model, &QAbstractItemModel::modelReset, this, &MessageHeader::updateGeometries);
  }
}

void MessageHeader::updateFilters() {
  auto m = qobject_cast<MessageModel*>(model());
  if (!m) return;

  QMap<int, QString> filters;
  for (auto [col, edit] : editors.asKeyValueRange()) {
    if (edit && !edit->text().isEmpty()) {
      filters[col] = edit->text();
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
    if (editors.contains(i)) continue;

    QString col_name = model()->headerData(i, Qt::Horizontal).toString();
    auto* edit = new DebouncedLineEdit(this);
    edit->setClearButtonEnabled(true);
    edit->setPlaceholderText(tr("Filter %1").arg(col_name));
    edit->setToolTip(getFilterTooltip(i));
    connect(edit, &DebouncedLineEdit::debouncedTextEdited, this, &MessageHeader::updateFilters);
    editors[i] = edit;
  }

  // 2. Update Viewport Margins
  if (auto first = editors.value(0)) {
    int required_h = first->sizeHint().height();
    if (viewportMargins().bottom() != required_h) {
      cached_editor_height = required_h;
      setViewportMargins(0, 0, 0, required_h);
    }
  }

  QHeaderView::updateGeometries();
  updateHeaderPositions();
  is_updating = false;
}

void MessageHeader::updateHeaderPositions() {
  const int header_h = QHeaderView::sizeHint().height();
  for (auto [col, edit] : editors.asKeyValueRange()) {
    if (edit) {
      edit->setGeometry(sectionViewportPosition(col), header_h, sectionSize(col), edit->sizeHint().height());
      edit->setHidden(isSectionHidden(col));
    }
  }
}

QSize MessageHeader::sizeHint() const {
  QSize sz = QHeaderView::sizeHint();
  int extra_h =
      cached_editor_height > 0 ? cached_editor_height : (editors.isEmpty() ? 0 : editors.first()->sizeHint().height());
  if (extra_h > 0) {
    sz.setHeight(sz.height() + extra_h + 1);
  }
  return sz;
}

QString MessageHeader::getFilterTooltip(int col) const {
  if (col == MessageModel::Column::SOURCE || col == MessageModel::Column::ADDRESS ||
      col == MessageModel::Column::FREQ || col == MessageModel::Column::COUNT) {
    QString tooltip =
        tr("<b>Range Filter</b><br>"
           "• Single value: <i>10</i><br>"
           "• Range: <i>10-20</i><br>"
           "• Minimum: <i>10-</i><br>"
           "• Maximum: <i>-20</i>");
    if (col == MessageModel::Column::ADDRESS) {
      tooltip += tr("<br><span style='color:gray;'>Values in Hexadecimal</span>");
    }
    return tooltip;
  }
  return col == MessageModel::Column::DATA ? tr("Filter by hex byte") : tr("Filter by name");
}
