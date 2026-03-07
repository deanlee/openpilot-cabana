#pragma once

#include <QStringList>

#include "file_stream.h"

class TrcLogStream : public FileStream {
  Q_OBJECT

 public:
  TrcLogStream(QObject* parent, const QStringList& file_paths);
};
