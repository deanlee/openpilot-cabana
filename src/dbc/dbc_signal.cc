
#include "dbc_signal.h"

#include <algorithm>
#include <cmath>

static int num_decimals(double num) {
  const QString string = QString::number(num);
  auto dot_pos = string.indexOf('.');
  return dot_pos == -1 ? 0 : string.size() - dot_pos - 1;
}

void cabana::Signal::update() {
  updateMsbLsb(*this);
  if (receiver_name.isEmpty()) {
    receiver_name = DEFAULT_NODE_NAME;
  }

  float h = 19 * (float)lsb / 64.0;
  h = std::fmod(h, 1.0);
  size_t hash = qHash(name);
  float s = 0.25 + 0.25 * (float)(hash & 0xff) / 255.0;
  float v = 0.75 + 0.25 * (float)((hash >> 8) & 0xff) / 255.0;

  color = QColor::fromHsvF(h, s, v);
  precision = std::max(num_decimals(factor), num_decimals(offset));
}

QString cabana::Signal::formatValue(double value, bool with_unit) const {
  // Show enum string
  int64_t raw_value = std::round((value - offset) / factor);
  for (const auto& [val, desc] : val_desc) {
    if (std::abs(raw_value - val) < 1e-6) {
      return desc;
    }
  }

  QString val_str = QString::number(value, 'f', precision);
  if (with_unit && !unit.isEmpty()) {
    val_str += " " + unit;
  }
  return val_str;
}

bool cabana::Signal::getValue(const uint8_t* data, size_t data_size, double* val) const {
  if (multiplexor && get_raw_value(data, data_size, *multiplexor) != multiplex_value) {
    return false;
  }
  *val = get_raw_value(data, data_size, *this);
  return true;
}

bool cabana::Signal::operator==(const cabana::Signal& other) const {
  return name == other.name && size == other.size &&
         start_bit == other.start_bit &&
         msb == other.msb && lsb == other.lsb &&
         is_signed == other.is_signed && is_little_endian == other.is_little_endian &&
         factor == other.factor && offset == other.offset &&
         min == other.min && max == other.max && comment == other.comment && unit == other.unit && val_desc == other.val_desc &&
         multiplex_value == other.multiplex_value && type == other.type && receiver_name == other.receiver_name;
}

double get_raw_value(const uint8_t* data, size_t data_size, const cabana::Signal& sig) {
  const int msb_byte = sig.msb / 8;
  if (msb_byte >= (int)data_size) return 0;

  const int lsb_byte = sig.lsb / 8;
  uint64_t val = 0;

  // Fast path: signal fits in a single byte
  if (msb_byte == lsb_byte) {
    val = (data[msb_byte] >> (sig.lsb & 7)) & ((1ULL << sig.size) - 1);
  } else {
    // Multi-byte case: signal spans across multiple bytes
    int bits = sig.size;
    int i = msb_byte;
    const int step = sig.is_little_endian ? -1 : 1;
    while (i >= 0 && i < (int)data_size && bits > 0) {
      const int msb = (i == msb_byte) ? sig.msb & 7 : 7;
      const int lsb = (i == lsb_byte) ? sig.lsb & 7 : 0;
      const int nbits = msb - lsb + 1;
      val = (val << nbits) | ((data[i] >> lsb) & ((1ULL << nbits) - 1));
      bits -= nbits;
      i += step;
    }
  }

  // Sign extension (if needed)
  if (sig.is_signed && (val & (1ULL << (sig.size - 1)))) {
    val |= ~((1ULL << sig.size) - 1);
  }

  return static_cast<int64_t>(val) * sig.factor + sig.offset;
}

void updateMsbLsb(cabana::Signal& s) {
  if (s.is_little_endian) {
    s.lsb = s.start_bit;
    s.msb = s.start_bit + s.size - 1;
  } else {
    s.lsb = flipBitPos(flipBitPos(s.start_bit) + s.size - 1);
    s.msb = s.start_bit;
  }
}
