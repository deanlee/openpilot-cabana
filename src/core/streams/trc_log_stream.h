#pragma once

#include <QStringList>
#include <vector>

#include "file_stream.h"

class TrcLogStream : public FileStream {
  Q_OBJECT

 public:
  TrcLogStream(QObject* parent, const QStringList& file_paths);

 protected:
  std::vector<ParsedCanFrame> parseFile(const QString& file_path) override;
};
