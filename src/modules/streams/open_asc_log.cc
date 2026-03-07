#include "open_asc_log.h"

#include <QApplication>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/streams/asc_log_stream.h"
#include "modules/settings/settings.h"

OpenAscLogWidget::OpenAscLogWidget(QWidget* parent) : AbstractStreamWidget(parent) {
  QVBoxLayout* main_layout = new QVBoxLayout(this);
  main_layout->addStretch(1);

  QHBoxLayout* file_layout = new QHBoxLayout();
  file_edit_ = new QLineEdit(this);
  file_edit_->setReadOnly(true);
  file_edit_->setPlaceholderText(tr("Select one or more Vector ASC log files (.asc)"));

  QPushButton* browse_btn = new QPushButton(tr("Browse..."), this);
  file_layout->addWidget(new QLabel(tr("ASC file"), this));
  file_layout->addWidget(file_edit_, 1);
  file_layout->addWidget(browse_btn);
  main_layout->addLayout(file_layout);

  main_layout->addStretch(1);
  setFocusProxy(file_edit_);
  emit enableOpenButton(false);

  connect(browse_btn, &QPushButton::clicked, this, [this]() {
    QStringList files = QFileDialog::getOpenFileNames(this, tr("Open ASC Log File(s)"),
                                                     settings.last_dir,
                                                     tr("ASC Log Files (*.asc);;All Files (*)"));
    if (!files.isEmpty()) {
      file_paths_ = files;
      settings.last_dir = QFileInfo(files.first()).absolutePath();
      file_edit_->setText(files.size() == 1 ? files.first()
                                            : tr("%1 files selected").arg(files.size()));
      emit enableOpenButton(true);
    }
  });
}

AbstractStream* OpenAscLogWidget::open() {
  if (file_paths_.isEmpty()) {
    QMessageBox::warning(this, tr("No file selected"), tr("Please select an ASC log file."));
    return nullptr;
  }

  auto* stream = new AscLogStream(qApp, file_paths_);
  if (stream->maxSeconds() == 0) {
    QMessageBox::warning(this, tr("Failed to open"),
                         tr("Could not parse any CAN frames from the selected file(s).\n\n"
                            "Ensure the files are valid Vector ASC logs."));
    delete stream;
    return nullptr;
  }
  return stream;
}
