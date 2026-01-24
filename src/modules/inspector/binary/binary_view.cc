#include "binary_view.h"

#include <QFontDatabase>
#include <QHeaderView>
#include <QMouseEvent>
#include <QScrollBar>
#include <QShortcut>
#include <QToolTip>

#include "core/commands/commands.h"
#include "modules/settings/settings.h"

inline int get_bit_pos(const QModelIndex &index) { return flipBitPos(index.row() * 8 + index.column()); }

BinaryView::BinaryView(QWidget *parent) : QTableView(parent) {
  model = new BinaryModel(this);
  setModel(model);
  delegate = new MessageBytesDelegate(this);

  setItemDelegate(delegate);
  horizontalHeader()->setMinimumSectionSize(0);
  horizontalHeader()->setDefaultSectionSize(CELL_WIDTH);
  horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

  horizontalHeader()->hide();

  verticalHeader()->setSectionsClickable(false);
  verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  verticalHeader()->setDefaultSectionSize(CELL_HEIGHT);


  setFrameStyle(QFrame::NoFrame);
  setShowGrid(false);
  setMouseTracking(true);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  connect(GetDBC(), &dbc::Manager::DBCFileChanged, this, &BinaryView::refresh);
  connect(UndoStack::instance(), &QUndoStack::indexChanged, this, &BinaryView::refresh);
  connect(model, &QAbstractItemModel::modelReset, this, &BinaryView::resetInternalState);

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

void BinaryView::setMessage(const MessageId &message_id) {
  model->msg_id = message_id;
  refresh();
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

void BinaryView::setSelection(const QRect &rect, QItemSelectionModel::SelectionFlags flags) {
  auto index = indexAt(viewport()->mapFromGlobal(QCursor::pos()));
  if (!anchor_index.isValid() || !index.isValid())
    return;

  QItemSelection selection;
  auto [start, size, is_lb] = getSelection(index);
  for (int i = 0; i < size; ++i) {
    int pos = is_lb ? flipBitPos(start + i) : flipBitPos(start) + i;
    selection << QItemSelectionRange{model->index(pos / 8, pos % 8)};
  }
  selectionModel()->select(selection, flags);
}

void BinaryView::mousePressEvent(QMouseEvent *event) {
  resize_sig = nullptr;
  if (auto index = indexAt(event->pos()); index.isValid() && index.column() != 8) {
    anchor_index = index;
    auto item = (const BinaryModel::Item *)anchor_index.internalPointer();
    int bit_pos = get_bit_pos(anchor_index);
    for (auto s : item->sigs) {
      if (bit_pos == s->lsb || bit_pos == s->msb) {
        int idx = flipBitPos(bit_pos == s->lsb ? s->msb : s->lsb);
        anchor_index = model->index(idx / 8, idx % 8);
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
  highlightPosition(event->globalPos());
  QTableView::mouseMoveEvent(event);
}

void BinaryView::mouseReleaseEvent(QMouseEvent *event) {
  QTableView::mouseReleaseEvent(event);

  auto release_index = indexAt(event->pos());
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

void BinaryView::refresh() {
  model->refresh();
  highlightPosition(QCursor::pos());
}

void BinaryView::resetInternalState() {
  anchor_index = QModelIndex();
  resize_sig = nullptr;
  hovered_sig = nullptr;
  verticalScrollBar()->setValue(0);
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
  if (index.column() == 8) {
    index = model->index(index.row(), 7);
  }
  bool is_lb = true;
  if (resize_sig) {
    is_lb = resize_sig->is_little_endian;
  } else if (settings.drag_direction == Settings::DragDirection::MsbFirst) {
    is_lb = index < anchor_index;
  } else if (settings.drag_direction == Settings::DragDirection::LsbFirst) {
    is_lb = !(index < anchor_index);
  } else if (settings.drag_direction == Settings::DragDirection::AlwaysLE) {
    is_lb = true;
  } else if (settings.drag_direction == Settings::DragDirection::AlwaysBE) {
    is_lb = false;
  }

  int cur_bit_pos = get_bit_pos(index);
  int anchor_bit_pos = get_bit_pos(anchor_index);
  int start_bit = is_lb ? std::min(cur_bit_pos, anchor_bit_pos) : get_bit_pos(std::min(index, anchor_index));
  int size = is_lb ? std::abs(cur_bit_pos - anchor_bit_pos) + 1 : std::abs(flipBitPos(cur_bit_pos) - flipBitPos(anchor_bit_pos)) + 1;
  return {start_bit, size, is_lb};
}
