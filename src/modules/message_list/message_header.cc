#include "message_header.h"

#include "message_model.h"

MessageHeader::MessageHeader(QWidget* parent) : QHeaderView(Qt::Horizontal, parent) {
  filter_timer.setSingleShot(true);
  filter_timer.setInterval(300);
  connect(&filter_timer, &QTimer::timeout, this, &MessageHeader::updateFilters);
  connect(this, &QHeaderView::sectionResized, this, &MessageHeader::updateHeaderPositions);
  connect(this, &QHeaderView::sectionMoved, this, &MessageHeader::updateHeaderPositions);
}

MessageHeader::~MessageHeader() {
  filter_timer.stop();
  editors.clear();
}

void MessageHeader::updateFilters() {
  QMap<int, QString> filters;
  for (int i = 0; i < count(); i++) {
    if (editors.value(i) && !editors[i]->text().isEmpty()) {
      filters[i] = editors[i]->text();
    }
  }
  auto m = qobject_cast<MessageModel*>(model());
  if (m) m->setFilterStrings(filters);
}

void MessageHeader::updateHeaderPositions() {
  QSize sz = QHeaderView::sizeHint();
  for (int i = 0; i < count(); i++) {
    if (editors.value(i)) {
      int h = editors[i]->sizeHint().height();
      editors[i]->setGeometry(sectionViewportPosition(i), sz.height(), sectionSize(i), h);
      editors[i]->setHidden(isSectionHidden(i));
    }
  }
}

void MessageHeader::updateGeometries() {
  if (!model() || count() <= 0) {
    QHeaderView::updateGeometries();
    return;
  }

  for (int i = 0; i < count(); i++) {
    if (!editors.value(i)) {
      QString column_name = model()->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString();
      QLineEdit* edit = new QLineEdit(this);
      edit->setClearButtonEnabled(true);
      edit->setPlaceholderText(tr("Filter %1").arg(column_name));

      connect(edit, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (text.isEmpty()) {
          filter_timer.stop();
          updateFilters();  // Instant clear
        } else {
          filter_timer.start();  // Debounced search
        }
      });
      editors[i] = edit;
    }
  }

  int required_margin = editors.value(0) ? editors.value(0)->sizeHint().height() : 0;
  if (viewportMargins().bottom() != required_margin) {
    setViewportMargins(0, 0, 0, required_margin);
  }

  QHeaderView::updateGeometries();
  updateHeaderPositions();
}

QSize MessageHeader::sizeHint() const {
  QSize sz = QHeaderView::sizeHint();

  auto first_editor = editors.value(0);
  if (first_editor) {
    int editor_h = first_editor->sizeHint().height();
    sz.setHeight(sz.height() + editor_h + 1);
  }

  return sz;
}
