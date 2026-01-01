#pragma once

#include <limits>
#include <utility>
#include <vector>

#include <QColor>
#include <QMetaType>
#include <QString>

const QString UNTITLED = "untitled";
const QString DEFAULT_NODE_NAME = "XXX";
constexpr int CAN_MAX_DATA_BYTES = 64;

#pragma pack(push, 1)
struct MessageId {
  union {
    struct {
      uint32_t address;
      uint8_t source;
    };
    uint64_t raw;
  };

  MessageId(uint8_t s = 0, uint32_t a = 0) : raw(0) {
    source = s;
    address = a;
  }

  QString toString() const {
    return QString("%1:%2").arg(source).arg(address, 0, 16).toUpper();
  }

  inline static MessageId fromString(const QString &str) {
    auto parts = str.split(':');
    if (parts.size() != 2) return {};
    return MessageId(uint8_t(parts[0].toUInt()), parts[1].toUInt(nullptr, 16));
  }

  bool operator==(const MessageId &other) const { return raw == other.raw; }
  bool operator!=(const MessageId &other) const { return raw != other.raw; }
  bool operator<(const MessageId &other) const { return raw < other.raw; }
  bool operator>(const MessageId &other) const { return raw > other.raw; }
};
#pragma pack(pop)

Q_DECLARE_METATYPE(MessageId);

template <>
struct std::hash<MessageId> {
  std::size_t operator()(const MessageId &k) const noexcept { return std::hash<uint64_t>{}(k.raw); }
};

typedef std::vector<std::pair<double, QString>> ValueDescription;

namespace cabana {

class Signal {
public:
  Signal() = default;
  Signal(const Signal &other) = default;
  void update();
  bool getValue(const uint8_t *data, size_t data_size, double *val) const;
  QString formatValue(double value, bool with_unit = true) const;
  bool operator==(const cabana::Signal &other) const;
  inline bool operator!=(const cabana::Signal &other) const { return !(*this == other); }

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
  Signal *multiplexor = nullptr;
};

class Msg {
public:
  Msg() = default;
  Msg(const Msg &other) { *this = other; }
  ~Msg();
  cabana::Signal *addSignal(const cabana::Signal &sig);
  cabana::Signal *updateSignal(const QString &sig_name, const cabana::Signal &sig);
  void removeSignal(const QString &sig_name);
  Msg &operator=(const Msg &other);
  int indexOf(const cabana::Signal *sig) const;
  cabana::Signal *sig(const QString &sig_name) const;
  QString newSignalName();
  void update();
  inline const std::vector<cabana::Signal *> &getSignals() const { return sigs; }

  uint32_t address;
  QString name;
  uint32_t size;
  QString comment;
  QString transmitter;
  std::vector<cabana::Signal *> sigs;

  std::vector<uint8_t> mask;
  cabana::Signal *multiplexor = nullptr;
};

}  // namespace cabana

// Helper functions
double get_raw_value(const uint8_t *data, size_t data_size, const cabana::Signal &sig);
void updateMsbLsb(cabana::Signal &s);
inline int flipBitPos(int start_bit) { return 8 * (start_bit / 8) + 7 - start_bit % 8; }
inline QString doubleToString(double value) { return QString::number(value, 'g', std::numeric_limits<double>::digits10); }
