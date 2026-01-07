#pragma

#include <QColor>
#include <QString>
#include <limits>
#include <vector>

const QString DEFAULT_NODE_NAME = "XXX";
constexpr int CAN_MAX_DATA_BYTES = 64;

typedef std::vector<std::pair<double, QString>> ValueDescription;

namespace cabana {

class Signal {
 public:
  Signal() = default;
  Signal(const Signal& other) = default;
  void update();
  bool getValue(const uint8_t* data, size_t data_size, double* val) const;
  QString formatValue(double value, bool with_unit = true) const;
  bool operator==(const cabana::Signal& other) const;
  inline bool operator!=(const cabana::Signal& other) const { return !(*this == other); }

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
  ValueDescription val_desc;
  int precision = 0;
  QColor color;

  // Multiplexed
  int multiplex_value = 0;
  Signal* multiplexor = nullptr;
};
}  // namespace cabana

// Helper functions
double get_raw_value(const uint8_t* data, size_t data_size, const cabana::Signal& sig);
void updateMsbLsb(cabana::Signal& s);
inline int flipBitPos(int start_bit) { return 8 * (start_bit / 8) + 7 - start_bit % 8; }
inline QString doubleToString(double value) { return QString::number(value, 'g', std::numeric_limits<double>::digits10); }
