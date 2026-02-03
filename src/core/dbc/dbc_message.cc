#include "dbc_message.h"

#include <algorithm>
#include <cmath>

#include "utils/util.h"

dbc::Msg::~Msg() {
  for (auto s : sigs) {
    delete s;
  }
}

dbc::Signal *dbc::Msg::addSignal(const dbc::Signal &sig) {
  auto s = sigs.emplace_back(new dbc::Signal(sig));
  update();
  return s;
}

dbc::Signal *dbc::Msg::updateSignal(const QString &sig_name, const dbc::Signal &new_sig) {
  auto s = sig(sig_name);
  if (s) {
    *s = new_sig;
    update();
  }
  return s;
}

void dbc::Msg::removeSignal(const QString &sig_name) {
  auto it = std::find_if(sigs.begin(), sigs.end(), [&](auto &s) { return s->name == sig_name; });
  if (it != sigs.end()) {
    delete *it;
    sigs.erase(it);
    update();
  }
}

dbc::Msg &dbc::Msg::operator=(const dbc::Msg &other) {
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

dbc::Signal *dbc::Msg::sig(const QString &sig_name) const {
  auto it = std::find_if(sigs.begin(), sigs.end(), [&](auto &s) { return s->name == sig_name; });
  return it != sigs.end() ? *it : nullptr;
}

int dbc::Msg::indexOf(const dbc::Signal *sig) const {
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
  std::sort(sigs.begin(), sigs.end(), [](auto l, auto r) {
    return std::tie(r->type, l->multiplex_value, l->start_bit, l->name) <
           std::tie(l->type, r->multiplex_value, r->start_bit, r->name);
  });

  for (auto *sig : sigs) {
    if (sig->type == dbc::Signal::Type::Multiplexor) {
      multiplexor = sig;
    }
    sig->update();

    // update mask
    for (int b = 0; b < sig->size; ++b) {
      int bit_idx = sig->getBitIndex(b);
      mask[bit_idx >> 3] |= (1 << (bit_idx & 7));
    }
  }

  for (auto *sig : sigs) {
    sig->multiplexor = sig->type == dbc::Signal::Type::Multiplexed ? multiplexor : nullptr;
    if (!sig->multiplexor) {
      if (sig->type == dbc::Signal::Type::Multiplexed) {
        sig->type = dbc::Signal::Type::Normal;
      }
      sig->multiplex_value = 0;
    }
  }
}
