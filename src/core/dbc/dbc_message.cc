#include "dbc_message.h"

#include <algorithm>
#include <cmath>

#include "dbc_manager.h"
#include "utils/util.h"

QString MessageId::toString() const {
  if (source == INVALID_SOURCE) {
    return QString("[%1]").arg(address, 0, 16).toUpper();
  }
  return QString("%1:%2").arg(source).arg(address, 0, 16).toUpper();
}

MessageId MessageId::fromString(const QString& str) {
  if (str.startsWith('[') && str.endsWith(']')) {
    bool ok;
    uint32_t addr = str.mid(1, str.size() - 2).toUInt(&ok, 16);
    return ok ? MessageId{INVALID_SOURCE, addr} : MessageId{};
  }

  const int sep = str.indexOf(':');
  if (sep == -1) return {};  // Return invalid ID if no separator

  bool ok_src, ok_addr;
  uint8_t src = static_cast<uint8_t>(str.left(sep).toUInt(&ok_src));
  uint32_t addr = str.mid(sep + 1).toUInt(&ok_addr, 16);

  if (!ok_src || !ok_addr) return {};
  return MessageId{src, addr};
}

// Msg

dbc::Msg::Msg(const Msg& other) { *this = other; }

dbc::Msg& dbc::Msg::operator=(const dbc::Msg& other) {
  if (this == &other) return *this;

  address = other.address;
  name = other.name;
  size = other.size;
  comment = other.comment;
  transmitter = other.transmitter;

  signals_.clear();
  signals_.reserve(other.signals_.size());
  for (const auto& s : other.signals_) {
    signals_.push_back(std::make_unique<Signal>(*s));
  }

  update();
  return *this;
}

dbc::Signal* dbc::Msg::addSignal(const dbc::Signal& sig) {
  auto* s = signals_.emplace_back(std::make_unique<Signal>(sig)).get();
  update();
  return s;
}

dbc::Signal* dbc::Msg::updateSignal(const QString& sig_name, const dbc::Signal& new_sig) {
  auto* s = findSignal(sig_name);
  if (s) {
    *s = new_sig;
    update();
  }
  return s;
}

void dbc::Msg::removeSignal(const QString& sig_name) {
  auto it = std::ranges::find(signals_, sig_name, [](const auto& p) -> const QString& { return p->name; });
  if (it != signals_.end()) {
    signals_.erase(it);
    update();
  }
}

dbc::Signal* dbc::Msg::findSignal(const QString& sig_name) const {
  auto it = std::ranges::find(sorted_sigs_, sig_name, &Signal::name);
  return it != sorted_sigs_.end() ? *it : nullptr;
}

int dbc::Msg::indexOf(const dbc::Signal* sig) const {
  auto it = std::ranges::find(sorted_sigs_, sig);
  return it != sorted_sigs_.end() ? static_cast<int>(it - sorted_sigs_.begin()) : -1;
}

QString dbc::Msg::newSignalName() const {
  for (int i = 1; /**/; ++i) {
    QString candidate = QString("NEW_SIGNAL_%1").arg(i);
    if (!findSignal(candidate)) return candidate;
  }
}

void dbc::Msg::update() {
  if (transmitter.isEmpty()) {
    transmitter = DEFAULT_NODE_NAME;
  }

  // Rebuild sorted raw-pointer view
  sorted_sigs_.clear();
  sorted_sigs_.reserve(signals_.size());
  for (const auto& s : signals_) {
    sorted_sigs_.push_back(s.get());
  }

  std::ranges::sort(sorted_sigs_, [](const Signal* l, const Signal* r) {
    return std::tie(r->type, l->multiplex_value, l->start_bit, l->name) <
           std::tie(l->type, r->multiplex_value, r->start_bit, r->name);
  });

  multiplexor_ = nullptr;
  for (auto* sig : sorted_sigs_) {
    sig->update();
    if (sig->type == Signal::Type::Multiplexor) {
      multiplexor_ = sig;
    }
  }

  rebuildMask();
  resolveMultiplexing();
}

void dbc::Msg::rebuildMask() {
  int aligned_size = ((size + 7) / 8) * 8;
  mask_.assign(aligned_size, 0x00);

  for (const auto* sig : sorted_sigs_) {
    int byte = sig->msb / 8;
    int bits = sig->size;
    while (byte >= 0 && byte < static_cast<int>(size) && bits > 0) {
      int lsb = (sig->lsb / 8 == byte) ? sig->lsb : byte * 8;
      int msb = (sig->msb / 8 == byte) ? sig->msb : (byte + 1) * 8 - 1;

      int nbits = msb - lsb + 1;
      mask_[byte] |= ((1ULL << nbits) - 1) << (lsb - byte * 8);

      bits -= nbits;
      byte += sig->is_little_endian ? -1 : 1;
    }
  }
}

void dbc::Msg::resolveMultiplexing() {
  for (auto* sig : sorted_sigs_) {
    if (sig->type == Signal::Type::Multiplexed && multiplexor_) {
      sig->multiplexor = multiplexor_;
    } else {
      sig->multiplexor = nullptr;
      if (sig->type == Signal::Type::Multiplexed) {
        sig->type = Signal::Type::Normal;
      }
      sig->multiplex_value = 0;
    }
  }
}
