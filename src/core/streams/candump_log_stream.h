#pragma once

#include <QHash>
#include <QStringList>
#include <vector>

#include "file_stream.h"

class CandumpLogStream : public FileStream {
  Q_OBJECT

 public:
  CandumpLogStream(QObject* parent, const QStringList& file_paths);

 protected:
  std::vector<ParsedCanFrame> parseFile(const QString& file_path) override;

 private:
  // Interface-to-bus map shared across files so the same interface always
  // gets the same bus number across a multi-file session.
  QHash<QString, uint8_t> iface_map_;
};
