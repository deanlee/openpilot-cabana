#pragma once

#include <QMetaType>
#include <QString>
#include <limits>
#include <memory>
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
  Msg(const Msg& other);
  Msg& operator=(const Msg& other);
  ~Msg() = default;

  Signal* addSignal(const Signal& sig);
  Signal* updateSignal(const QString& sig_name, const Signal& new_sig);
  void removeSignal(const QString& sig_name);
  Signal* findSignal(const QString& sig_name) const;
  int indexOf(const Signal* sig) const;
  QString newSignalName() const;
  void update();

  const std::vector<Signal*>& getSignals() const { return sorted_sigs_; }
  size_t signalCount() const { return signals_.size(); }
  const std::vector<uint8_t>& getMask() const { return mask_; }
  Signal* getMultiplexor() const { return multiplexor_; }

  uint32_t address = 0;
  QString name;
  uint32_t size = 0;
  QString comment;
  QString transmitter;

 private:
  void rebuildMask();
  void resolveMultiplexing();

  std::vector<std::unique_ptr<Signal>> signals_;

  // Derived state, rebuilt by update()
  std::vector<Signal*> sorted_sigs_;
  std::vector<uint8_t> mask_;
  Signal* multiplexor_ = nullptr;
};

}  // namespace dbc
