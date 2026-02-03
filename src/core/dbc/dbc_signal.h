#pragma

#include <QColor>
#include <QString>
#include <limits>
#include <vector>

const QString DEFAULT_NODE_NAME = "XXX";

using ValueTable = std::vector<std::pair<double, QString>>;

namespace dbc {

class Signal {
 public:
  Signal() = default;
  Signal(const Signal& other) = default;
  int getBitIndex(int i) const;  // Logic-to-CAN mapping
  uint64_t decodeRaw(const uint8_t* data, size_t data_size) const;
  double toPhysical(const uint8_t* data, size_t data_size) const;
  void update();
  bool parse(const uint8_t* data, size_t data_size, double* val) const;
  QString formatValue(double value, bool with_unit = true) const;
  bool operator==(const dbc::Signal& other) const;
  inline bool operator!=(const dbc::Signal& other) const { return !(*this == other); }

  enum class Type {
    Normal = 0,
    Multiplexed,
    Multiplexor
  };

  Type type = Type::Normal;
  QString name;
  int start_bit, msb, lsb, size;
  double factor = 1.0;
  double offset = 0;
  bool is_signed;
  bool is_little_endian;
  double min, max;
  QString unit;
  QString comment;
  QString receiver_name;
  ValueTable value_table;
  int precision = 0;
  QColor color;

  // Multiplexed
  int multiplex_value = 0;
  Signal* multiplexor = nullptr;
};
}  // namespace cabana

// Helper functions
void updateMsbLsb(dbc::Signal& s);
inline int flipBitPos(int pos) { return (pos & ~7) | (7 - (pos & 7)); }
