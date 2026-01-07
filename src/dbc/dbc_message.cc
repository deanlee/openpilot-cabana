#include "dbc/dbc_message.h"

#include <algorithm>
#include <cmath>

#include "utils/util.h"

cabana::Msg::~Msg() {
  for (auto s : sigs) {
    delete s;
  }
}

cabana::Signal *cabana::Msg::addSignal(const cabana::Signal &sig) {
  auto s = sigs.emplace_back(new cabana::Signal(sig));
  update();
  return s;
}

cabana::Signal *cabana::Msg::updateSignal(const QString &sig_name, const cabana::Signal &new_sig) {
  auto s = sig(sig_name);
  if (s) {
    *s = new_sig;
    update();
  }
  return s;
}

void cabana::Msg::removeSignal(const QString &sig_name) {
  auto it = std::find_if(sigs.begin(), sigs.end(), [&](auto &s) { return s->name == sig_name; });
  if (it != sigs.end()) {
    delete *it;
    sigs.erase(it);
    update();
  }
}

cabana::Msg &cabana::Msg::operator=(const cabana::Msg &other) {
  address = other.address;
  name = other.name;
  size = other.size;
  comment = other.comment;
  transmitter = other.transmitter;

  for (auto s : sigs) delete s;
  sigs.clear();
  for (auto s : other.sigs) {
    sigs.push_back(new cabana::Signal(*s));
  }

  update();
  return *this;
}

cabana::Signal *cabana::Msg::sig(const QString &sig_name) const {
  auto it = std::find_if(sigs.begin(), sigs.end(), [&](auto &s) { return s->name == sig_name; });
  return it != sigs.end() ? *it : nullptr;
}

int cabana::Msg::indexOf(const cabana::Signal *sig) const {
  for (int i = 0; i < sigs.size(); ++i) {
    if (sigs[i] == sig) return i;
  }
  return -1;
}

QString cabana::Msg::newSignalName() {
  QString new_name;
  for (int i = 1; /**/; ++i) {
    new_name = QString("NEW_SIGNAL_%1").arg(i);
    if (sig(new_name) == nullptr) break;
  }
  return new_name;
}

void cabana::Msg::update() {
  if (transmitter.isEmpty()) {
    transmitter = DEFAULT_NODE_NAME;
  }

  // Align to 8-byte boundaries
  int aligned_size = ((size + 7) / 8) * 8;
  mask.assign(aligned_size, 0x00);

  multiplexor = nullptr;

  // sort signals
  std::sort(sigs.begin(), sigs.end(), [](auto l, auto r) {
    return std::tie(r->type, l->multiplex_value, l->start_bit, l->name) <
           std::tie(l->type, r->multiplex_value, r->start_bit, r->name);
  });

  for (auto sig : sigs) {
    if (sig->type == cabana::Signal::Type::Multiplexor) {
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
    sig->multiplexor = sig->type == cabana::Signal::Type::Multiplexed ? multiplexor : nullptr;
    if (!sig->multiplexor) {
      if (sig->type == cabana::Signal::Type::Multiplexed) {
        sig->type = cabana::Signal::Type::Normal;
      }
      sig->multiplex_value = 0;
    }
  }
}
