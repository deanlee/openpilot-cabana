#include "binary_view.h"

#include <QFontDatabase>
#include <QHeaderView>
#include <QMouseEvent>
#include <QScrollBar>
#include <QShortcut>
#include <QToolTip>

#include "core/commands/commands.h"
#include "modules/settings/settings.h"

inline int absoluteBitIndex(const QModelIndex& index) { return index.row() * 8 + (7 - index.column()); }

BinaryView::BinaryView(QWidget* parent) : QTableView(parent) {
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

  setupShortcuts();
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

void BinaryView::setupShortcuts() {
  auto bindKeys = [this](const QList<Qt::Key>& keys, auto&& func) {
    for (auto key : keys) {
      QShortcut* s = new QShortcut(QKeySequence(key), this);
      connect(s, &QShortcut::activated, this, func);
    }
  };

  // Delete Signal (x, backspace, delete)
  bindKeys({Qt::Key_X, Qt::Key_Backspace, Qt::Key_Delete}, [this] {
    if (hovered_signal) {
      UndoStack::push(new RemoveSigCommand(model->message_id, hovered_signal));
      hovered_signal = nullptr;
    }
  });

  // Change endianness (e)
  bindKeys({Qt::Key_E}, [this] {
    if (hovered_signal) {
      dbc::Signal s = *hovered_signal;
      s.is_little_endian = !s.is_little_endian;
      emit editSignal(hovered_signal, s);
    }
  });

  // Change signedness (s)
  bindKeys({Qt::Key_S}, [this] {
    if (hovered_signal) {
      dbc::Signal s = *hovered_signal;
      s.is_signed = !s.is_signed;
      emit editSignal(hovered_signal, s);
    }
  });

  // Open chart (c, p, g)
  bindKeys({Qt::Key_P, Qt::Key_G, Qt::Key_C}, [this] {
    if (hovered_signal) emit showChart(model->message_id, hovered_signal, true, false);
  });
}

QSize BinaryView::minimumSizeHint() const {
  // (9 columns * width) + the vertical header + 2px buffer for the frame
  int totalWidth = (CELL_WIDTH * 9) + CELL_WIDTH + 2;
  // Show at least 4 rows, at most 10
  int totalHeight = CELL_HEIGHT * std::min(model->rowCount(), 10) + 2;
  return {totalWidth, totalHeight};
}

void BinaryView::highlightSignal(const dbc::Signal* sig) {
  if (sig != hovered_signal) {
    if (sig) model->updateSignalCells(sig);
    if (hovered_signal) model->updateSignalCells(hovered_signal);

    hovered_signal = sig;
    emit signalHovered(hovered_signal);
  }
}

void BinaryView::mousePressEvent(QMouseEvent* event) {
  resizing_signal = nullptr;
  if (auto index = indexAt(event->pos()); index.isValid() && index.column() != 8) {
    anchor_index_ = index;
    const auto *item = model->getItem(anchor_index_);
    int clicked_bit = absoluteBitIndex(anchor_index_);
    for (auto s : item->signal_list) {
      if (clicked_bit == s->lsb || clicked_bit == s->msb) {
        int other_bit = (clicked_bit == s->lsb) ? s->msb : s->lsb;
        anchor_index_ = model->index(other_bit / 8, 7 - (other_bit % 8));
        resizing_signal = s;
        break;
      }
    }
  }
  event->accept();
}

void BinaryView::highlightSignalAtPosition(const QPoint& pos) {
  if (auto index = indexAt(viewport()->mapFromGlobal(pos)); index.isValid()) {
    const auto *item = model->getItem(index);
    const dbc::Signal* sig = item->signal_list.isEmpty() ? nullptr : item->signal_list.back();
    highlightSignal(sig);
  }
}

void BinaryView::mouseMoveEvent(QMouseEvent* event) {
  highlightSignalAtPosition(event->globalPosition().toPoint());
  QTableView::mouseMoveEvent(event);
}

void BinaryView::mouseReleaseEvent(QMouseEvent* event) {
  QTableView::mouseReleaseEvent(event);

  auto release_index = indexAt(event->position().toPoint());
  if (release_index.isValid() && anchor_index_.isValid()) {
    if (selectionModel()->hasSelection()) {
      auto sig = resizing_signal ? *resizing_signal : dbc::Signal{};
      std::tie(sig.start_bit, sig.size, sig.is_little_endian) = calculateSelection(release_index);
      resizing_signal ? emit editSignal(resizing_signal, sig) : UndoStack::push(new AddSigCommand(model->message_id, sig));
    } else {
      const auto *item = model->getItem(anchor_index_);
      if (item && item->signal_list.size() > 0) emit signalClicked(item->signal_list.back());
    }
  }
  clearSelection();
  anchor_index_ = QModelIndex();
  resizing_signal = nullptr;
}

void BinaryView::leaveEvent(QEvent* event) {
  highlightSignal(nullptr);
  QTableView::leaveEvent(event);
}

void BinaryView::resetInternalState() {
  anchor_index_ = QModelIndex();
  resizing_signal = nullptr;
  hovered_signal = nullptr;
  verticalScrollBar()->setValue(0);
  highlightSignalAtPosition(QCursor::pos());
}

std::tuple<int, int, bool> BinaryView::calculateSelection(QModelIndex index) {
  if (index.column() == 8) index = model->index(index.row(), 7);

  bool is_le = true;
  if (resizing_signal) {
    is_le = resizing_signal->is_little_endian;
  } else if (settings.drag_direction == Settings::DragDirection::MsbFirst) {
    is_le = index < anchor_index_;
  } else if (settings.drag_direction == Settings::DragDirection::LsbFirst) {
    is_le = !(index < anchor_index_);
  } else if (settings.drag_direction == Settings::DragDirection::AlwaysLE) {
    is_le = true;
  } else if (settings.drag_direction == Settings::DragDirection::AlwaysBE) {
    is_le = false;
  }

  int cur_bit = absoluteBitIndex(index);
  int anchor_bit = absoluteBitIndex(anchor_index_);

  int start_bit, size;
  if (is_le) {
    // Intel: Start bit is numerically lowest
    start_bit = std::min(cur_bit, anchor_bit);
    size = std::abs(cur_bit - anchor_bit) + 1;
  } else {
    // Motorola: Start bit is "Visual Top-Left".
    auto cursor_pos = std::make_pair(index.row(), index.column());
    auto anchor_pos = std::make_pair(anchor_index_.row(), anchor_index_.column());

    QModelIndex top_left_index = (cursor_pos < anchor_pos) ? index : anchor_index_;

    start_bit = absoluteBitIndex(top_left_index);
    size = std::abs(flipBitPos(cur_bit) - flipBitPos(anchor_bit)) + 1;
  }

  return {start_bit, size, is_le};
}

void BinaryView::setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags flags) {
  auto cur_idx = indexAt(viewport()->mapFromGlobal(QCursor::pos()));
  if (!anchor_index_.isValid() || !cur_idx.isValid()) return;

  auto [start, size, is_le] = calculateSelection(cur_idx);
  QItemSelection selection;

  for (int j = 0; j < size; ++j) {
    int abs_bit = is_le ? (start + j) : flipBitPos(flipBitPos(start) + j);
    selection << QItemSelectionRange{model->index(abs_bit / 8, 7 - (abs_bit % 8))};
  }
  selectionModel()->select(selection, flags);
}
