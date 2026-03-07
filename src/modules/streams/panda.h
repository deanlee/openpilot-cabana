#pragma once

#include <QComboBox>
#include <QFormLayout>

#include "abstract.h"
#include "core/streams/panda_stream.h"

class PandaWidget : public AbstractStreamWidget {
  Q_OBJECT

 public:
  PandaWidget(QWidget* parent = nullptr);
  AbstractStream* open() override;

 private:
  void refreshSerials();
  void buildConfigForm();

  QComboBox* serial_edit;
  QFormLayout* form_layout;
  PandaStreamConfig config = {};
};
