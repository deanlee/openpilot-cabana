#include "tools/findsignal.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <cmath>

#include "modules/system/stream_manager.h"
#include "widgets/validators.h"

// ─── FindSignalModel ─────────────────────────────────────────────────────────

QVariant FindSignalModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (role != Qt::DisplayRole) return {};
  if (orientation == Qt::Vertical) return QString::number(section + 1);
  static const QString titles[] = {"Message", "Signal (start|size)", "Prev Value", "Value", "Δ"};
  return titles[section];
}

QVariant FindSignalModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= (int)filtered_signals.size()) return {};
  const auto& s = filtered_signals[index.row()];
  if (role == Qt::DisplayRole) {
    switch (index.column()) {
      case 0: return s.id.toString();
      case 1: {
        const char endian = s.sig.is_little_endian ? 'L' : 'B';
        const char sign   = s.sig.is_signed ? 'S' : 'U';
        return QString("%1|%2 [%3%4]").arg(s.sig.start_bit).arg(s.sig.size).arg(endian).arg(sign);
      }
      case 2: return histories.size() > 1 ? QString::number(s.prev_value, 'g', 6) : QStringLiteral("—");
      case 3: return QString::number(s.value, 'g', 6);
      case 4: {
        if (histories.size() <= 1) return QStringLiteral("—");
        const double delta = s.value - s.prev_value;
        return delta > 0 ? QString("+%1").arg(delta, 0, 'g', 4) : QString::number(delta, 'g', 4);
      }
    }
  }
  if (role == Qt::ForegroundRole && index.column() == 4 && histories.size() > 1) {
    const double delta = s.value - s.prev_value;
    if (delta > 0) return QColor(0x22, 0xc5, 0x5e);
    if (delta < 0) return QColor(0xef, 0x44, 0x44);
  }
  return {};
}

// On the first scan the values are already captured inside initial_signals;
// on subsequent scans we look up the value at scan_ns and compare against the
// stored (previous) value, then update the stored value for the next step.
void FindSignalModel::search(uint64_t scan_ns, std::function<bool(double, double)> cmp) {
  beginResetModel();

  const bool is_first = histories.isEmpty();
  const auto& candidates = is_first ? initial_signals : histories.back();

  filtered_signals.clear();
  filtered_signals.reserve(candidates.size());

  if (is_first) {
    for (const auto& s : candidates) {
      if (cmp(s.prev_value, s.value)) filtered_signals.push_back(s);
    }
  } else {
    std::mutex lock;
    QtConcurrent::blockingMap(candidates, [&](const SearchSignal& s) {
      const auto& events = StreamManager::stream()->events(s.id);
      auto it = std::ranges::upper_bound(events, scan_ns, {}, &CanEvent::mono_ns);
      if (it == events.cbegin()) return;
      --it;
      const double new_value = s.sig.toPhysical((*it)->dat, (*it)->size);
      if (!cmp(s.value, new_value)) return;
      SearchSignal updated = s;
      updated.mono_ns    = (*it)->mono_ns;
      updated.prev_value = s.value;
      updated.value      = new_value;
      std::lock_guard lk(lock);
      filtered_signals.push_back(std::move(updated));
    });
    std::ranges::sort(filtered_signals, [](const SearchSignal& a, const SearchSignal& b) {
      if (a.id != b.id) return a.id < b.id;
      if (a.sig.start_bit != b.sig.start_bit) return a.sig.start_bit < b.sig.start_bit;
      return a.sig.size < b.sig.size;
    });
  }

  histories.push_back(filtered_signals);
  endResetModel();
}

void FindSignalModel::undo() {
  if (!histories.isEmpty()) {
    beginResetModel();
    histories.pop_back();
    filtered_signals.clear();
    if (!histories.isEmpty()) filtered_signals = histories.back();
    endResetModel();
  }
}

void FindSignalModel::reset() {
  beginResetModel();
  histories.clear();
  filtered_signals.clear();
  initial_signals.clear();
  endResetModel();
}

// ─── FindSignalDlg ───────────────────────────────────────────────────────────

FindSignalDlg::FindSignalDlg(QWidget* parent) : QDialog(parent, Qt::WindowFlags() | Qt::Window) {
  setWindowTitle(tr("Find Signal"));
  setAttribute(Qt::WA_DeleteOnClose);
  auto* main_layout = new QVBoxLayout(this);

  // Messages group
  message_group = new QGroupBox(tr("Messages"), this);
  auto* message_layout = new QFormLayout(message_group);
  message_layout->addRow(tr("Bus"), bus_edit = new QLineEdit());
  bus_edit->setPlaceholderText(tr("comma-separated. Leave blank for all"));
  message_layout->addRow(tr("Address"), address_edit = new QLineEdit());
  address_edit->setPlaceholderText(tr("comma-separated hex. Leave blank for all"));
  auto* time_row = new QHBoxLayout();
  time_row->addWidget(scan_time_edit = new QLineEdit("0"));
  scan_time_edit->setFixedWidth(80);
  auto* now_btn = new QPushButton(tr("Now"), this);
  now_btn->setFixedWidth(50);
  time_row->addWidget(now_btn);
  time_row->addWidget(new QLabel(tr("sec")));
  time_row->addStretch();
  message_layout->addRow(tr("Scan At"), time_row);

  // Signal properties group
  properties_group = new QGroupBox(tr("Signal"), this);
  auto* property_layout = new QFormLayout(properties_group);
  property_layout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
  auto* size_row = new QHBoxLayout();
  size_row->addWidget(min_size = new QSpinBox);
  size_row->addWidget(new QLabel("-"));
  size_row->addWidget(max_size = new QSpinBox);
  size_row->addWidget(litter_endian = new QCheckBox(tr("Little endian")));
  size_row->addWidget(is_signed = new QCheckBox(tr("Signed")));
  size_row->addStretch();
  min_size->setRange(1, 64);
  max_size->setRange(1, 64);
  min_size->setValue(8);
  max_size->setValue(8);
  litter_endian->setChecked(true);
  property_layout->addRow(tr("Size"), size_row);
  property_layout->addRow(tr("Factor"), factor_edit = new QLineEdit("1.0"));
  property_layout->addRow(tr("Offset"), offset_edit = new QLineEdit("0.0"));

  // Scan group
  auto* find_group = new QGroupBox(tr("Scan"), this);
  auto* vlayout = new QVBoxLayout(find_group);
  auto* hlayout = new QHBoxLayout();
  hlayout->addWidget(new QLabel(tr("Value")));
  hlayout->addWidget(compare_cb = new QComboBox(this));
  hlayout->addWidget(value1 = new QLineEdit);
  hlayout->addWidget(to_label = new QLabel("-"));
  hlayout->addWidget(value2 = new QLineEdit);
  hlayout->addWidget(undo_btn = new QPushButton(tr("Undo"), this));
  hlayout->addWidget(search_btn = new QPushButton(tr("First Scan"), this));
  hlayout->addWidget(reset_btn = new QPushButton(tr("New Scan"), this));
  vlayout->addLayout(hlayout);
  vlayout->addWidget(view = new QTableView(this));

  // Compare modes: indices 0-7 are absolute, 8-13 are relative (Next Scan)
  compare_cb->addItems({
    tr("Any Value"),
    tr("= Exact Value"),
    tr("≠ Not Equal"),
    tr("> Greater Than"),
    tr("≥ Greater or Equal"),
    tr("< Less Than"),
    tr("≤ Less or Equal"),
    tr("Between A…B"),
    tr("↕ Changed"),
    tr("= Unchanged"),
    tr("↑ Increased"),
    tr("↓ Decreased"),
    tr("↑ Increased By"),
    tr("↓ Decreased By"),
  });

  value1->setVisible(false);
  value2->setVisible(false);
  to_label->setVisible(false);
  undo_btn->setEnabled(false);
  reset_btn->setEnabled(false);

  auto* double_validator = new DoubleValidator(this);
  for (auto* edit : {value1, value2, factor_edit, offset_edit, scan_time_edit})
    edit->setValidator(double_validator);

  view->setContextMenuPolicy(Qt::CustomContextMenu);
  view->horizontalHeader()->setStretchLastSection(true);
  view->horizontalHeader()->setSelectionMode(QAbstractItemView::NoSelection);
  view->setSelectionBehavior(QAbstractItemView::SelectRows);
  view->setModel(model = new FindSignalModel(this));

  auto* filter_row = new QHBoxLayout();
  filter_row->addWidget(message_group);
  filter_row->addWidget(properties_group);
  main_layout->addLayout(filter_row);
  main_layout->addWidget(find_group);
  main_layout->addWidget(stats_label = new QLabel(
      tr("Set scan time, signal properties and value condition, then click First Scan.")));

  setMinimumSize({700, 650});

  connect(search_btn, &QPushButton::clicked, this, &FindSignalDlg::search);
  connect(undo_btn,   &QPushButton::clicked, model, &FindSignalModel::undo);
  connect(reset_btn,  &QPushButton::clicked, model, &FindSignalModel::reset);
  connect(model, &QAbstractItemModel::modelReset, this, &FindSignalDlg::modelReset);
  connect(view,  &QTableView::customContextMenuRequested, this, &FindSignalDlg::customMenuRequested);
  connect(view,  &QTableView::doubleClicked, [this](const QModelIndex& index) {
    if (index.isValid()) emit openMessage(model->filtered_signals[index.row()].id);
  });
  connect(compare_cb, qOverload<int>(&QComboBox::currentIndexChanged),
          this, &FindSignalDlg::updateValueVisibility);
  connect(now_btn, &QPushButton::clicked, [this]() {
    if (auto* can = StreamManager::stream())
      scan_time_edit->setText(QString::number(can->currentSec(), 'f', 3));
  });
}

void FindSignalDlg::updateValueVisibility(int idx) {
  // Absolute modes 1-7 and "Increased/Decreased By" (12-13) need value1.
  // "Between" (7) also needs value2.
  const bool needs_v1 = (idx >= 1 && idx <= 7) || idx >= 12;
  const bool needs_v2 = (idx == 7);
  value1->setVisible(needs_v1);
  to_label->setVisible(needs_v2);
  value2->setVisible(needs_v2);
}

void FindSignalDlg::search() {
  auto* can = StreamManager::stream();
  if (!can) return;

  const double scan_sec = scan_time_edit->text().toDouble();
  const uint64_t scan_ns = can->toMonoNs(scan_sec);
  const bool is_first = model->histories.isEmpty();

  if (is_first) {
    setInitialSignals(scan_ns);
    if (model->initial_signals.isEmpty()) {
      stats_label->setText(tr("No CAN events found in the selected messages at this time."));
      stats_label->setVisible(true);
      return;
    }
  }

  const int idx = compare_cb->currentIndex();
  const double v1 = value1->text().toDouble();
  const double v2 = value2->text().toDouble();

  std::function<bool(double, double)> cmp;
  switch (idx) {
    case 0:  cmp = [](double, double)            { return true; };                           break;
    case 1:  cmp = [v1](double, double nv)       { return nv == v1; };                       break;
    case 2:  cmp = [v1](double, double nv)       { return nv != v1; };                       break;
    case 3:  cmp = [v1](double, double nv)       { return nv > v1; };                        break;
    case 4:  cmp = [v1](double, double nv)       { return nv >= v1; };                       break;
    case 5:  cmp = [v1](double, double nv)       { return nv < v1; };                        break;
    case 6:  cmp = [v1](double, double nv)       { return nv <= v1; };                       break;
    case 7:  cmp = [v1, v2](double, double nv)   { return nv >= v1 && nv <= v2; };           break;
    case 8:  cmp = [](double pv, double nv)      { return nv != pv; };                       break;
    case 9:  cmp = [](double pv, double nv)      { return nv == pv; };                       break;
    case 10: cmp = [](double pv, double nv)      { return nv > pv; };                        break;
    case 11: cmp = [](double pv, double nv)      { return nv < pv; };                        break;
    case 12: cmp = [v1](double pv, double nv)    { return std::abs((nv - pv) - v1) < 1e-9; }; break;
    case 13: cmp = [v1](double pv, double nv)    { return std::abs((pv - nv) - v1) < 1e-9; }; break;
    default: return;
  }

  message_group->setEnabled(false);
  properties_group->setEnabled(false);
  search_btn->setEnabled(false);
  stats_label->setVisible(false);
  search_btn->setText(tr("Scanning…"));
  QTimer::singleShot(0, this, [this, scan_ns, cmp = std::move(cmp)]() { model->search(scan_ns, cmp); });
}

void FindSignalDlg::setInitialSignals(uint64_t scan_ns) {
  QSet<ushort> buses;
  for (auto bus : bus_edit->text().trimmed().split(",")) {
    if (bus = bus.trimmed(); !bus.isEmpty()) buses.insert(bus.toUShort());
  }
  QSet<uint32_t> addresses;
  for (auto addr : address_edit->text().trimmed().split(",")) {
    if (addr = addr.trimmed(); !addr.isEmpty()) addresses.insert(addr.toULong(nullptr, 16));
  }

  dbc::Signal sig{};
  sig.is_little_endian = litter_endian->isChecked();
  sig.is_signed = is_signed->isChecked();
  sig.factor = factor_edit->text().toDouble();
  sig.offset = offset_edit->text().toDouble();

  auto* can = StreamManager::stream();
  model->initial_signals.clear();

  for (const auto& [id, m] : can->snapshots()) {
    if ((!buses.isEmpty() && !buses.contains(id.source)) ||
        (!addresses.isEmpty() && !addresses.contains(id.address)))
      continue;

    const auto& events = can->events(id);
    // Find the latest event at or before scan_ns
    auto it = std::ranges::upper_bound(events, scan_ns, {}, &CanEvent::mono_ns);
    if (it == events.cbegin()) continue;
    --it;

    const int total_bits = m->size * 8;
    for (int size = min_size->value(); size <= max_size->value(); ++size) {
      for (int start = 0; start <= total_bits - size; ++start) {
        FindSignalModel::SearchSignal s;
        s.id            = id;
        s.mono_ns       = (*it)->mono_ns;
        s.sig           = sig;
        s.sig.start_bit = start;
        s.sig.size      = size;
        s.sig.update();
        s.prev_value    = 0.;
        s.value         = s.sig.toPhysical((*it)->dat, (*it)->size);
        model->initial_signals.push_back(s);
      }
    }
  }
}

void FindSignalDlg::modelReset() {
  const bool is_first = model->histories.isEmpty();
  message_group->setEnabled(is_first);
  properties_group->setEnabled(is_first);
  search_btn->setEnabled(!model->filtered_signals.isEmpty() || is_first);
  search_btn->setText(is_first ? tr("First Scan") : tr("Next Scan"));
  reset_btn->setEnabled(!is_first);
  undo_btn->setEnabled(model->histories.size() > 1);
  stats_label->setVisible(true);
  if (is_first) {
    stats_label->setText(tr("Set scan time, signal properties and value condition, then click First Scan."));
  } else {
    stats_label->setText(tr("%1 candidate(s) remaining. Right-click to create signal, double-click to open message.")
                             .arg(model->filtered_signals.size()));
  }
}

void FindSignalDlg::customMenuRequested(const QPoint& pos) {
  const auto index = view->indexAt(pos);
  if (!index.isValid()) return;
  QMenu menu(this);
  menu.addAction(tr("Create Signal"));
  if (menu.exec(view->mapToGlobal(pos))) {
    auto& s = model->filtered_signals[index.row()];
    UndoStack::push(new AddSigCommand(s.id, s.sig));
    emit openMessage(s.id);
  }
}

