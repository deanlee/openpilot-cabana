#pragma once

#include <limits>
#include <utility>
#include <vector>

#include <QMetaType>
#include <QString>

#include "dbc_signal.h"

const QString UNTITLED = "untitled";

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

namespace cabana {

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
