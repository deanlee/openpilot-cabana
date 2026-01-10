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
    connect(model, &QAbstractItemModel::modelAboutToBeReset, this, &MessageHeader::clearEditors);
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
  if (!model() || count() <= 0) {
    QHeaderView::updateGeometries();
    return;
  }

  // 1. Sync Editors with Column Count
  for (int i = 0; i < count(); ++i) {
    if (!editors.value(i)) {
      QString col_name = model()->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString();
      QLineEdit* edit = new QLineEdit(this);
      edit->setClearButtonEnabled(true);
      edit->setPlaceholderText(tr("Filter %1").arg(col_name));

      // Connect with 'this' context for safety
      connect(edit, &QLineEdit::textChanged, this, [this](const QString& text) {
        text.isEmpty() ? (filter_timer.stop(), updateFilters()) : filter_timer.start();
      });
      editors[i] = edit;
    }
  }

  // 2. Recursion Guard for Margins
  int required_h = editors.value(0) ? editors[0]->sizeHint().height() : 0;
  if (viewportMargins().bottom() != required_h) {
    cached_editor_height = required_h;
    setViewportMargins(0, 0, 0, required_h);
  }

  QHeaderView::updateGeometries();
  updateHeaderPositions();
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