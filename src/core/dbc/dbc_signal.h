#pragma once

#include <QColor>
#include <QString>
#include <limits>
#include <optional>
#include <vector>

const QString DEFAULT_NODE_NAME = "XXX";

using ValueTable = std::vector<std::pair<double, QString>>;

namespace dbc {

class Signal {
 public:
  Signal() = default;
  Signal(const Signal& other) = default;

  void update();
  int getBitIndex(int i) const;
  uint64_t decodeRaw(const uint8_t* data, size_t data_size) const;
  double toPhysical(const uint8_t* data, size_t data_size) const;
  std::optional<double> parse(const uint8_t* data, size_t data_size) const;
  QString formatValue(double value, bool with_unit = true) const;
  bool operator==(const Signal& other) const;
  bool operator!=(const Signal& other) const { return !(*this == other); }

  static int flipBitPos(int pos) { return (pos & ~7) | (7 - (pos & 7)); }

  enum class Type { Normal = 0, Multiplexed, Multiplexor };

  // Persistent state (serialized to DBC)
  Type type = Type::Normal;
  QString name;
  int start_bit = 0;
  int size = 0;
  double factor = 1.0;
  double offset = 0;
  bool is_signed = false;
  bool is_little_endian = true;
  double min = 0;
  double max = 0;
  QString unit;
  QString comment;
  QString receiver_name;
  ValueTable value_table;
  int multiplex_value = 0;
  Signal* multiplexor = nullptr;

  // Derived state (rebuilt by update())
  int msb = 0;
  int lsb = 0;
  int precision = 0;
  QColor color;

 private:
  void computeMsbLsb();
  void computeColor();
};

}  // namespace dbc
