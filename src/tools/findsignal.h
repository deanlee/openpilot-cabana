#pragma once

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableView>
#include <algorithm>
#include <functional>
#include <limits>

#include "core/commands/commands.h"
#include "modules/settings/settings.h"

class FindSignalModel : public QAbstractTableModel {
 public:
  struct SearchSignal {
    MessageId id = {};
    uint64_t mono_ns = 0;
    dbc::Signal sig = {};
    double value = 0.;       // value captured at the most recent scan
    double prev_value = 0.;  // value captured at the previous scan (shown as Δ)
  };

  FindSignalModel(QObject* parent) : QAbstractTableModel(parent) {}
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override { return 5; }
  int rowCount(const QModelIndex& parent = QModelIndex()) const override {
    return std::min<int>((int)(filtered_signals.size()), 500);
  }
  void search(uint64_t scan_ns, std::function<bool(double, double)> cmp);
  void reset();
  void undo();

  QList<SearchSignal> filtered_signals;
  QList<SearchSignal> initial_signals;
  QList<QList<SearchSignal>> histories;
};

class FindSignalDlg : public QDialog {
  Q_OBJECT
 public:
  FindSignalDlg(QWidget* parent);

 signals:
  void openMessage(const MessageId& id);

 private:
  void search();
  void modelReset();
  void setInitialSignals(uint64_t scan_ns);
  void customMenuRequested(const QPoint& pos);
  void updateValueVisibility(int cmp_index);

  QLineEdit *value1, *value2, *factor_edit, *offset_edit;
  QLineEdit *bus_edit, *address_edit, *scan_time_edit;
  QComboBox* compare_cb;
  QSpinBox *min_size, *max_size;
  QCheckBox *little_endian, *is_signed;
  QPushButton *search_btn, *reset_btn, *undo_btn;
  QGroupBox *properties_group, *message_group;
  QTableView* view;
  QLabel *to_label, *stats_label;
  FindSignalModel* model;
};
