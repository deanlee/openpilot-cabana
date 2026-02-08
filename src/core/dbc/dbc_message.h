#pragma once

#include <QMetaType>
#include <QString>
#include <limits>
#include <utility>
#include <vector>

#include "dbc_signal.h"

const QString UNDEFINED = "Undefined";
constexpr int INVALID_SOURCE = 0xff;

struct MessageId {
  uint32_t address;
  uint8_t source;

  MessageId(uint8_t s = 0, uint32_t a = 0) : address(a), source(s) {}

  inline uint64_t v() const noexcept { return (static_cast<uint64_t>(source) << 32) | address; }

  bool operator==(const MessageId& other) const noexcept { return v() == other.v(); }
  bool operator!=(const MessageId& other) const noexcept { return v() != other.v(); }
  bool operator<(const MessageId& other) const noexcept { return v() < other.v(); }
  bool operator>(const MessageId& other) const noexcept { return v() > other.v(); }

  QString toString() const;
  static MessageId fromString(const QString& str);
};

Q_DECLARE_METATYPE(MessageId);

template <>
struct std::hash<MessageId> {
  std::size_t operator()(const MessageId& k) const noexcept {
    uint64_t x = k.v();
    // SplitMix64 Finalizer: Constant time (~1ns), prevents O(N) lookup cliffs
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);

    return static_cast<std::size_t>(x);
  }
};

namespace dbc {

class Msg {
 public:
  Msg() = default;
  Msg(const Msg& other) { *this = other; }
  ~Msg();
  dbc::Signal* addSignal(const dbc::Signal& sig);
  dbc::Signal* updateSignal(const QString& sig_name, const dbc::Signal& sig);
  void removeSignal(const QString& sig_name);
  Msg& operator=(const Msg& other);
  int indexOf(const dbc::Signal* sig) const;
  dbc::Signal* sig(const QString& sig_name) const;
  QString newSignalName();
  void update();
  inline const std::vector<dbc::Signal*>& getSignals() const { return sigs; }

  uint32_t address;
  QString name;
  uint32_t size;
  QString comment;
  QString transmitter;
  std::vector<dbc::Signal*> sigs;

  std::vector<uint8_t> mask;
  dbc::Signal* multiplexor = nullptr;
};

}  // namespace dbc
