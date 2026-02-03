#pragma

#include <QColor>
#include <QString>
#include <limits>
#include <vector>

const QString DEFAULT_NODE_NAME = "XXX";

using ValueTable = std::vector<std::pair<double, QString>>;

namespace dbc {

struct ByteStep {
  uint16_t index;       // Byte index in the CAN frame
  uint8_t right_shift;  // How much to shift the raw byte to align to 0
  uint8_t mask;         // The mask to apply after right-shifting
  uint8_t nbits;        // How many bits this byte contributes to the signal
  uint8_t left_shift;   // How much to shift this chunk to its final position
};

struct SignalDecodePlan {
  ByteStep steps[9];
  uint8_t num_steps;
  uint8_t size;
  bool is_signed;
  double factor;
  double offset;
};

class Signal {
 public:
  Signal() = default;
  Signal(const Signal& other) = default;
  int getBitIndex(int i) const;  // Logic-to-CAN mapping
  void update();
  bool getValue(const uint8_t* data, size_t data_size, double* val) const;
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
  SignalDecodePlan decode_plan;
};

}  // namespace dbc

// Helper functions
double decodeSignal(const uint8_t* data, size_t data_size, const dbc::Signal& sig);
void updateMsbLsb(dbc::Signal& s);
inline int flipBitPos(int pos) { return (pos & ~7) | (7 - (pos & 7)); }
