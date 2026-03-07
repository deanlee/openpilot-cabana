#pragma once

#include <QStringList>

#include "file_stream.h"

class CandumpLogStream : public FileStream {
  Q_OBJECT

 public:
  CandumpLogStream(QObject* parent, const QStringList& file_paths);
};
