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
dbc::Msg::~Msg() {
  for (auto s : sigs) {
    delete s;
  }
}

dbc::Signal* dbc::Msg::addSignal(const dbc::Signal& sig) {
  auto s = sigs.emplace_back(new dbc::Signal(sig));
  update();
  return s;
}

dbc::Signal* dbc::Msg::updateSignal(const QString& sig_name, const dbc::Signal& new_sig) {
  auto s = sig(sig_name);
  if (s) {
    *s = new_sig;
    update();
  }
  return s;
}

void dbc::Msg::removeSignal(const QString& sig_name) {
  auto it = std::ranges::find(sigs, sig_name, &dbc::Signal::name);
  if (it != sigs.end()) {
    delete *it;
    sigs.erase(it);
    update();
  }
}

dbc::Msg& dbc::Msg::operator=(const dbc::Msg& other) {
  address = other.address;
  name = other.name;
  size = other.size;
  comment = other.comment;
  transmitter = other.transmitter;

  for (auto s : sigs) delete s;
  sigs.clear();
  for (auto s : other.sigs) {
    sigs.push_back(new dbc::Signal(*s));
  }

  update();
  return *this;
}

dbc::Signal* dbc::Msg::sig(const QString& sig_name) const {
  auto it = std::ranges::find(sigs, sig_name, &dbc::Signal::name);
  return it != sigs.end() ? *it : nullptr;
}

int dbc::Msg::indexOf(const dbc::Signal* sig) const {
  for (int i = 0; i < sigs.size(); ++i) {
    if (sigs[i] == sig) return i;
  }
  return -1;
}

QString dbc::Msg::newSignalName() {
  QString new_name;
  for (int i = 1; /**/; ++i) {
    new_name = QString("NEW_SIGNAL_%1").arg(i);
    if (sig(new_name) == nullptr) break;
  }
  return new_name;
}

void dbc::Msg::update() {
  if (transmitter.isEmpty()) {
    transmitter = DEFAULT_NODE_NAME;
  }

  // Align to 8-byte boundaries
  int aligned_size = ((size + 7) / 8) * 8;
  mask.assign(aligned_size, 0x00);

  multiplexor = nullptr;

  // sort signals
  std::ranges::sort(sigs, [](const auto* l, const auto* r) {
    return std::tie(r->type, l->multiplex_value, l->start_bit, l->name) <
           std::tie(l->type, r->multiplex_value, r->start_bit, r->name);
  });

  for (auto sig : sigs) {
    if (sig->type == dbc::Signal::Type::Multiplexor) {
      multiplexor = sig;
    }
    sig->update();

    // update mask
    int i = sig->msb / 8;
    int bits = sig->size;
    while (i >= 0 && i < size && bits > 0) {
      int lsb = (int)(sig->lsb / 8) == i ? sig->lsb : i * 8;
      int msb = (int)(sig->msb / 8) == i ? sig->msb : (i + 1) * 8 - 1;

      int sz = msb - lsb + 1;
      int shift = (lsb - (i * 8));

      mask[i] |= ((1ULL << sz) - 1) << shift;

      bits -= sz;
      i = sig->is_little_endian ? i - 1 : i + 1;
    }
  }

  for (auto sig : sigs) {
    sig->multiplexor = sig->type == dbc::Signal::Type::Multiplexed ? multiplexor : nullptr;
    if (!sig->multiplexor) {
      if (sig->type == dbc::Signal::Type::Multiplexed) {
        sig->type = dbc::Signal::Type::Normal;
      }
      sig->multiplex_value = 0;
    }
  }
}
