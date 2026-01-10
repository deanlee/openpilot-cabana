#include "message_header.h"

#include "message_model.h"

MessageHeader::MessageHeader(QWidget* parent) : QHeaderView(Qt::Horizontal, parent) {
  filter_timer.setSingleShot(true);
  filter_timer.setInterval(300);
  connect(&filter_timer, &QTimer::timeout, this, &MessageHeader::updateFilters);
  connect(this, &QHeaderView::sectionResized, this, &MessageHeader::updateHeaderPositions);
  connect(this, &QHeaderView::sectionMoved, this, &MessageHeader::updateHeaderPositions);
}

void MessageHeader::updateFilters() {
  QMap<int, QString> filters;
  for (int i = 0; i < count(); i++) {
    if (editors[i] && !editors[i]->text().isEmpty()) {
      filters[i] = editors[i]->text();
    }
  }
  qobject_cast<MessageModel*>(model())->setFilterStrings(filters);
}

void MessageHeader::updateHeaderPositions() {
  QSize sz = QHeaderView::sizeHint();
  for (int i = 0; i < count(); i++) {
    if (editors[i]) {
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
    if (!editors[i]) {
      QString column_name = model()->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString();
      editors[i] = new QLineEdit(this);
      editors[i]->setClearButtonEnabled(true);
      editors[i]->setPlaceholderText(tr("Filter %1").arg(column_name));

      connect(editors[i], &QLineEdit::textChanged, this, [this](const QString& text) {
        if (text.isEmpty()) {
          filter_timer.stop();
          updateFilters();  // Instant clear
        } else {
          filter_timer.start();  // Debounced search
        }
      });
    }
  }

  int required_margin = editors[0] ? editors[0]->sizeHint().height() : 0;
  if (viewportMargins().bottom() != required_margin) {
    setViewportMargins(0, 0, 0, required_margin);
  }

  QHeaderView::updateGeometries();
  updateHeaderPositions();
}


QSize MessageHeader::sizeHint() const {
  QSize sz = QHeaderView::sizeHint();
  return editors[0] ? QSize(sz.width(), sz.height() + editors[0]->height() + 1) : sz;
}
