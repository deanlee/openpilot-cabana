#pragma once

#include <QStringList>

#include "file_stream.h"

class AscLogStream : public FileStream {
  Q_OBJECT

 public:
  AscLogStream(QObject* parent, const QStringList& file_paths);
};
