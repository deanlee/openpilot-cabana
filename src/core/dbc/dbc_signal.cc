
#include "dbc_signal.h"

#include <algorithm>
#include <cmath>

#include "utils/util.h"

void dbc::Signal::update() {
  updateMsbLsb(*this);
  if (receiver_name.isEmpty()) {
    receiver_name = DEFAULT_NODE_NAME;
  }

  // 1. Hue: Use Golden Ratio distribution or a larger prime multiplier
  // to prevent similar Hues for adjacent signals.
  // 360 degrees * ((lsb * golden_ratio) mod 1)
  float h = std::fmod((float)lsb * 0.61803398875f, 1.0f);

  // 2. Saturation: High enough to be "colorful" in charts,
  // but low enough to allow text readability.
  size_t hash = qHash(name);
  float s = 0.4f + 0.2f * (float)(hash & 0xff) / 255.0f;  // Range: 0.4 - 0.6

  // 3. Value (Brightness): Keep it high so black text always has contrast.
  // For the Binary View, you can control the "heat" using Alpha later.
  float v = 0.85f + 0.15f * (float)((hash >> 8) & 0xff) / 255.0f;  // Range: 0.85 - 1.0

  color = QColor::fromHsvF(h, s, v);
  precision = std::max(utils::num_decimals(factor), utils::num_decimals(offset));
}

int dbc::Signal::getBitIndex(int i) const {
  if (is_little_endian) {
    return start_bit + i;
  } else {
    // Motorola Big Endian Sawtooth
    return flipBitPos(flipBitPos(start_bit) + i);
  }
}

QString dbc::Signal::formatValue(double value, bool with_unit) const {
  // Show enum string
  if (!value_table.empty()) {
    int64_t raw_value = std::round((value - offset) / factor);
    for (const auto& [val, desc] : value_table) {
      if (std::abs(raw_value - val) < 1e-6) {
        return desc;
      }
    }
  }

  QString val_str = QString::number(value, 'f', precision);
  if (with_unit && !unit.isEmpty()) {
    val_str += " " + unit;
  }
  return val_str;
}

bool dbc::Signal::parse(const uint8_t* data, size_t data_size, double* val) const {
  if (multiplexor && multiplexor->decodeRaw(data, data_size) != multiplex_value) {
    return false;
  }
  *val = toPhysical(data, data_size);
  return true;
}

bool dbc::Signal::operator==(const dbc::Signal& other) const {
  return name == other.name && size == other.size &&
         start_bit == other.start_bit &&
         msb == other.msb && lsb == other.lsb &&
         is_signed == other.is_signed && is_little_endian == other.is_little_endian &&
         factor == other.factor && offset == other.offset &&
         min == other.min && max == other.max && comment == other.comment && unit == other.unit && value_table == other.value_table &&
         multiplex_value == other.multiplex_value && type == other.type && receiver_name == other.receiver_name;
}

uint64_t dbc::Signal::decodeRaw(const uint8_t* data, size_t data_size) const {
  const int msb_byte = msb / 8;
  if (msb_byte >= (int)data_size) return 0;

  const int lsb_byte = lsb / 8;
  uint64_t val = 0;

  // Fast path: signal fits in a single byte
  if (msb_byte == lsb_byte) {
    val = (data[msb_byte] >> (lsb & 7)) & ((1ULL << size) - 1);
  } else {
    // Multi-byte case: signal spans across multiple bytes
    int bits = size;
    int i = msb_byte;
    const int step = is_little_endian ? -1 : 1;
    while (i >= 0 && i < (int)data_size && bits > 0) {
      const int cur_msb = (i == msb_byte) ? (msb & 7) : 7;
      const int cur_lsb = (i == lsb_byte) ? (lsb & 7) : 0;
      const int nbits = cur_msb - cur_lsb + 1;
      val = (val << nbits) | ((data[i] >> cur_lsb) & ((1ULL << nbits) - 1));
      bits -= nbits;
      i += step;
    }
  }
  return val;
}

double dbc::Signal::toPhysical(const uint8_t* data, size_t data_size) const {
  uint64_t val = decodeRaw(data, data_size);

  // Sign extension
  if (is_signed && (val & (1ULL << (size - 1)))) {
    val |= ~((1ULL << size) - 1);
    return static_cast<int64_t>(val) * factor + offset;
  }

  return val * factor + offset;
}

void updateMsbLsb(dbc::Signal& s) {
  int end_bit = s.getBitIndex(s.size - 1);
  if (s.is_little_endian) {
    s.lsb = s.start_bit;
    s.msb = end_bit;
  } else {
    s.msb = s.start_bit;
    s.lsb = end_bit;
  }
}
