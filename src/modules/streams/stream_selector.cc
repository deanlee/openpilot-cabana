#include "stream_selector.h"

#include <QFileDialog>
#include <QLabel>
#include <QPushButton>

#include "modules/settings/settings.h"
#include "asc_log.h"
#include "candump_log.h"
#include "device.h"
#include "panda.h"
#include "route.h"
#include "socketcan.h"
#include "trc_log.h"

StreamSelector::StreamSelector(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Open stream"));
  QVBoxLayout* layout = new QVBoxLayout(this);
  tab = new QTabWidget(this);
  layout->addWidget(tab);

  QHBoxLayout* dbc_layout = new QHBoxLayout();
  dbc_file = new QLineEdit(this);
  dbc_file->setReadOnly(true);
  dbc_file->setPlaceholderText(tr("Choose a dbc file to open"));
  QPushButton* browse_btn = new QPushButton(tr("Browse..."));
  dbc_layout->addWidget(new QLabel(tr("dbc File")));
  dbc_layout->addWidget(dbc_file);
  dbc_layout->addWidget(browse_btn);
  layout->addLayout(dbc_layout);

  QFrame* line = new QFrame(this);
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

  addStreamWidget(new RouteWidget, tr("&Route"));
  addStreamWidget(new AscLogWidget, tr("&ASC Log"));
  addStreamWidget(new CandumpLogWidget, tr("&candump"));
  addStreamWidget(new TrcLogWidget, tr("&TRC"));
  addStreamWidget(new PandaWidget, tr("&Panda"));
  if (SocketCanStream::available()) {
    addStreamWidget(new SocketCanWidget, tr("&SocketCAN"));
  }
  addStreamWidget(new DeviceWidget, tr("&Device"));

  connect(btn_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(btn_box, &QDialogButtonBox::accepted, [=]() {
    setEnabled(false);
    if (stream_ = ((AbstractStreamWidget*)tab->currentWidget())->open(); stream_) {
      accept();
    }
    setEnabled(true);
  });
  connect(browse_btn, &QPushButton::clicked, [this]() {
    QString fn = QFileDialog::getOpenFileName(this, tr("Open File"), settings.last_dir, "DBC (*.dbc)");
    if (!fn.isEmpty()) {
      dbc_file->setText(fn);
      settings.last_dir = QFileInfo(fn).absolutePath();
    }
  });
}

void StreamSelector::addStreamWidget(AbstractStreamWidget* w, const QString& title) {
  tab->addTab(w, title);
  auto open_btn = btn_box->button(QDialogButtonBox::Open);
  connect(w, &AbstractStreamWidget::enableOpenButton, open_btn, &QPushButton::setEnabled);
}
