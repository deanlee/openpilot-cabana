#include "stream_selector.h"

#include <QFileDialog>
#include <QLabel>
#include <QPushButton>

#include "open_device.h"
#include "open_panda.h"
#include "open_replay.h"
#include "open_socketcan.h"
#include "modules/settings/settings.h"

StreamSelector::StreamSelector(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Open stream"));
  QVBoxLayout *layout = new QVBoxLayout(this);
  tab = new QTabWidget(this);
  layout->addWidget(tab);

  QHBoxLayout *dbc_layout = new QHBoxLayout();
  dbc_file = new QLineEdit(this);
  dbc_file->setReadOnly(true);
  dbc_file->setPlaceholderText(tr("Choose a dbc file to open"));
  QPushButton *file_btn = new QPushButton(tr("Browse..."));
  dbc_layout->addWidget(new QLabel(tr("dbc File")));
  dbc_layout->addWidget(dbc_file);
  dbc_layout->addWidget(file_btn);
  layout->addLayout(dbc_layout);

  QFrame *line = new QFrame(this);
  line->setFrameStyle(QFrame::HLine | QFrame::Sunken);
  layout->addWidget(line);

  btn_box = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel);
  layout->addWidget(btn_box);

    tab->setFocusPolicy(Qt::TabFocus);
  connect(tab, &QTabWidget::currentChanged, [this](int index) {
  if (QWidget* w = tab->widget(index)) {
    w->setFocus();
  }
});


  addStreamWidget(new OpenReplayWidget, tr("&Replay"));
  addStreamWidget(new OpenPandaWidget, tr("&Panda"));
  if (SocketCanStream::available()) {
    addStreamWidget(new OpenSocketCanWidget, tr("&SocketCAN"));
  }
  addStreamWidget(new OpenDeviceWidget, tr("&Device"));

  connect(btn_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(btn_box, &QDialogButtonBox::accepted, [=]() {
    setEnabled(false);
    if (stream_ = ((AbstractStreamWidget *)tab->currentWidget())->open(); stream_) {
      accept();
    }
    setEnabled(true);
  });
  connect(file_btn, &QPushButton::clicked, [this]() {
    QString fn = QFileDialog::getOpenFileName(this, tr("Open File"), settings.last_dir, "DBC (*.dbc)");
    if (!fn.isEmpty()) {
      dbc_file->setText(fn);
      settings.last_dir = QFileInfo(fn).absolutePath();
    }
  });
}

void StreamSelector::addStreamWidget(AbstractStreamWidget *w, const QString &title) {
  tab->addTab(w, title);
  auto open_btn = btn_box->button(QDialogButtonBox::Open);
  connect(w, &AbstractStreamWidget::enableOpenButton, open_btn, &QPushButton::setEnabled);
}
