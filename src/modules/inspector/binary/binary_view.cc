#include "binary_view.h"

#include <QFontDatabase>
#include <QHeaderView>
#include <QMouseEvent>
#include <QScrollBar>
#include <QShortcut>
#include <QToolTip>

#include "core/commands/commands.h"
#include "modules/settings/settings.h"

inline int get_abs_bit(const QModelIndex& index) {
  return index.row() * 8 + (7 - index.column());
}

BinaryView::BinaryView(QWidget *parent) : QTableView(parent) {
  delegate = new MessageBytesDelegate(this);

  setItemDelegate(delegate);
  horizontalHeader()->setMinimumSectionSize(0);
  horizontalHeader()->setDefaultSectionSize(CELL_WIDTH);
  horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

  horizontalHeader()->hide();

  verticalHeader()->setSectionsClickable(false);
  verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  verticalHeader()->setDefaultSectionSize(CELL_HEIGHT);


  setShowGrid(false);
  setMouseTracking(true);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  addShortcuts();
  setWhatsThis(R"(
    <b>Binary View</b><br/>
    <!-- TODO: add descprition here -->
    <span style="color:gray">Shortcuts</span><br />
    Delete Signal:
      <span style="background-color:lightGray;color:gray">&nbsp;x&nbsp;</span>,
      <span style="background-color:lightGray;color:gray">&nbsp;Backspace&nbsp;</span>,
      <span style="background-color:lightGray;color:gray">&nbsp;Delete&nbsp;</span><br />
    Change endianness: <span style="background-color:lightGray;color:gray">&nbsp;e&nbsp; </span><br />
    Change singedness: <span style="background-color:lightGray;color:gray">&nbsp;s&nbsp;</span><br />
    Open chart:
      <span style="background-color:lightGray;color:gray">&nbsp;c&nbsp;</span>,
      <span style="background-color:lightGray;color:gray">&nbsp;p&nbsp;</span>,
      <span style="background-color:lightGray;color:gray">&nbsp;g&nbsp;</span>
  )");
}

void BinaryView::setModel(QAbstractItemModel* newModel) {
  model = static_cast<BinaryModel*>(newModel);
  QTableView::setModel(model);
  if (model) {
    connect(model, &QAbstractItemModel::modelReset, this, &BinaryView::resetInternalState);
  }
}

void BinaryView::addShortcuts() {
  auto bindKeys = [this](const QList<Qt::Key>& keys, auto&& func) {
    for (auto key : keys) {
      QShortcut *s = new QShortcut(QKeySequence(key), this);
      connect(s, &QShortcut::activated, this, func);
    }
  };

  // Delete Signal (x, backspace, delete)
  bindKeys({Qt::Key_X, Qt::Key_Backspace, Qt::Key_Delete}, [this] {
    if (hovered_sig) {
      UndoStack::push(new RemoveSigCommand(model->msg_id, hovered_sig));
      hovered_sig = nullptr;
    }
  });

  // Change endianness (e)
  bindKeys({Qt::Key_E}, [this] {
    if (hovered_sig) {
      dbc::Signal s = *hovered_sig;
      s.is_little_endian = !s.is_little_endian;
      emit editSignal(hovered_sig, s);
    }
  });

  // Change signedness (s)
  bindKeys({Qt::Key_S}, [this] {
    if (hovered_sig) {
      dbc::Signal s = *hovered_sig;
      s.is_signed = !s.is_signed;
      emit editSignal(hovered_sig, s);
    }
  });

  // Open chart (c, p, g)
  bindKeys({Qt::Key_P, Qt::Key_G, Qt::Key_C}, [this] {
    if (hovered_sig) emit showChart(model->msg_id, hovered_sig, true, false);
  });
}

QSize BinaryView::minimumSizeHint() const {
  // (9 columns * width) + the vertical header + 2px buffer for the frame
  int totalWidth = (CELL_WIDTH * 9) + CELL_WIDTH + 2;
  // Show at least 4 rows, at most 10
  int totalHeight = CELL_HEIGHT * std::min(model->rowCount(), 10) + 2;
  return {totalWidth, totalHeight};
}

void BinaryView::highlight(const dbc::Signal *sig) {
  if (sig != hovered_sig) {
    for (int i = 0; i < model->items.size(); ++i) {
      auto &item_sigs = model->items[i].sigs;
      if ((sig && item_sigs.contains(sig)) || (hovered_sig && item_sigs.contains(hovered_sig))) {
        auto index = model->index(i / model->columnCount(), i % model->columnCount());
        emit model->dataChanged(index, index, {Qt::DisplayRole});
      }
    }

    hovered_sig = sig;
    emit signalHovered(hovered_sig);
  }
}

void BinaryView::mousePressEvent(QMouseEvent* event) {
  resize_sig = nullptr;
  if (auto index = indexAt(event->pos()); index.isValid() && index.column() != 8) {
    anchor_index = index;
    auto item = (const BinaryModel::Item*)anchor_index.internalPointer();
    int clicked_bit = get_abs_bit(anchor_index);
    for (auto s : item->sigs) {
      if (clicked_bit == s->lsb || clicked_bit == s->msb) {
        int other_bit = (clicked_bit == s->lsb) ? s->msb : s->lsb;
        anchor_index = model->index(other_bit / 8, 7 - (other_bit % 8));
        resize_sig = s;
        break;
      }
    }
  }
  event->accept();
}

void BinaryView::highlightPosition(const QPoint &pos) {
  if (auto index = indexAt(viewport()->mapFromGlobal(pos)); index.isValid()) {
    auto item = (BinaryModel::Item *)index.internalPointer();
    const dbc::Signal *sig = item->sigs.isEmpty() ? nullptr : item->sigs.back();
    highlight(sig);
  }
}

void BinaryView::mouseMoveEvent(QMouseEvent *event) {
  highlightPosition(event->globalPosition().toPoint());
  QTableView::mouseMoveEvent(event);
}

void BinaryView::mouseReleaseEvent(QMouseEvent *event) {
  QTableView::mouseReleaseEvent(event);

  auto release_index = indexAt(event->position().toPoint());
  if (release_index.isValid() && anchor_index.isValid()) {
    if (selectionModel()->hasSelection()) {
      auto sig = resize_sig ? *resize_sig : dbc::Signal{};
      std::tie(sig.start_bit, sig.size, sig.is_little_endian) = getSelection(release_index);
      resize_sig ? emit editSignal(resize_sig, sig)
                 : UndoStack::push(new AddSigCommand(model->msg_id, sig));
    } else {
      auto item = (const BinaryModel::Item *)anchor_index.internalPointer();
      if (item && item->sigs.size() > 0)
        emit signalClicked(item->sigs.back());
    }
  }
  clearSelection();
  anchor_index = QModelIndex();
  resize_sig = nullptr;
}

void BinaryView::leaveEvent(QEvent *event) {
  highlight(nullptr);
  QTableView::leaveEvent(event);
}

void BinaryView::resetInternalState() {
  anchor_index = QModelIndex();
  resize_sig = nullptr;
  hovered_sig = nullptr;
  verticalScrollBar()->setValue(0);
  highlightPosition(QCursor::pos());
}

QSet<const dbc::Signal *> BinaryView::getOverlappingSignals() const {
  QSet<const dbc::Signal *> overlapping;
  for (const auto &item : model->items) {
    if (item.sigs.size() > 1) {
      for (auto s : item.sigs) {
        if (s->type == dbc::Signal::Type::Normal) overlapping += s;
      }
    }
  }
  return overlapping;
}

std::tuple<int, int, bool> BinaryView::getSelection(QModelIndex index) {
  if (index.column() == 8) index = model->index(index.row(), 7);

  bool is_le = true;
  if (resize_sig) {
    is_le = resize_sig->is_little_endian;
  } else if (settings.drag_direction == Settings::DragDirection::MsbFirst) {
    is_le = index < anchor_index;
  } else if (settings.drag_direction == Settings::DragDirection::LsbFirst) {
    is_le = !(index < anchor_index);
  } else if (settings.drag_direction == Settings::DragDirection::AlwaysLE) {
    is_le = true;
  } else if (settings.drag_direction == Settings::DragDirection::AlwaysBE) {
    is_le = false;
  }

  int cur_bit = get_abs_bit(index);
  int anchor_bit = get_abs_bit(anchor_index);

  int start_bit, size;
  if (is_le) {
    // Intel: Start bit is numerically lowest
    start_bit = std::min(cur_bit, anchor_bit);
    size = std::abs(cur_bit - anchor_bit) + 1;
  } else {
    // Motorola: Start bit is "Visual Top-Left".
    auto pos_cur = std::make_pair(index.row(), index.column());
    auto pos_anchor = std::make_pair(anchor_index.row(), anchor_index.column());

    QModelIndex topLeft = (pos_cur < pos_anchor) ? index : anchor_index;

    start_bit = get_abs_bit(topLeft);
    size = std::abs(flipBitPos(cur_bit) - flipBitPos(anchor_bit)) + 1;
  }

  return {start_bit, size, is_le};
}

void BinaryView::setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags flags) {
  auto cur_idx = indexAt(viewport()->mapFromGlobal(QCursor::pos()));
  if (!anchor_index.isValid() || !cur_idx.isValid()) return;

  auto [start, size, is_le] = getSelection(cur_idx);
  QItemSelection selection;

  for (int j = 0; j < size; ++j) {
    int abs_bit = is_le ? (start + j) : flipBitPos(flipBitPos(start) + j);
    selection << QItemSelectionRange{model->index(abs_bit / 8, 7 - (abs_bit % 8))};
  }
  selectionModel()->select(selection, flags);
}
