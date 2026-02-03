
#include "dbc_signal.h"

#include <algorithm>
#include <cmath>

#include "utils/util.h"

dbc::SignalDecodePlan buildBytePlan(const dbc::Signal& sig) {
  dbc::SignalDecodePlan plan{};
  plan.size = sig.size;
  plan.is_signed = sig.is_signed;
  plan.factor = sig.factor;
  plan.offset = sig.offset;
  plan.num_steps = 0;

  int bits_remaining = sig.size;
  // Normalized logic: start at the most significant byte
  int current_byte = sig.is_little_endian ? (sig.msb / 8) : (sig.msb / 8);
  int step_dir = sig.is_little_endian ? -1 : 1;

  // Phase 1: Identify chunks
  while (bits_remaining > 0 && plan.num_steps < 9) {
    int byte_msb = (current_byte == (sig.msb / 8)) ? (sig.msb % 8) : 7;
    int byte_lsb = (current_byte == (sig.lsb / 8)) ? (sig.lsb % 8) : 0;

    int nbits = byte_msb - byte_lsb + 1;
    if (nbits > bits_remaining) nbits = bits_remaining;

    plan.steps[plan.num_steps++] = {
        static_cast<uint16_t>(current_byte),
        static_cast<uint8_t>(byte_lsb),
        static_cast<uint8_t>((1U << nbits) - 1),
        static_cast<uint8_t>(nbits),
        0  // Calculated next
    };

    bits_remaining -= nbits;
    current_byte += step_dir;
  }

  // Phase 2: Calculate Parallel Destination Shifts
  int bits_placed = 0;
  for (int i = 0; i < plan.num_steps; ++i) {
    // The first step contains the MSB bits, so it shifts left the most
    plan.steps[i].left_shift = plan.size - bits_placed - plan.steps[i].nbits;
    bits_placed += plan.steps[i].nbits;
  }

  return plan;
}

void dbc::Signal::update() {
  updateMsbLsb(*this);
  decode_plan = buildBytePlan(*this);

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

bool dbc::Signal::getValue(const uint8_t* data, size_t data_size, double* val) const {
  if (multiplexor && decodeSignal(data, data_size, *multiplexor) != multiplex_value) {
    return false;
  }
  *val = decodeSignal(data, data_size, *this);
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

double decodeSignal(const uint8_t* data, size_t data_size, const dbc::Signal& sig) {
  uint64_t val = 0;

  const auto& steps = sig.decode_plan.steps;
  for (uint8_t i = 0; i < sig.decode_plan.num_steps; ++i) {
    const auto& s = steps[i];

    if (s.index < data_size) {
      uint64_t chunk = (static_cast<uint64_t>(data[s.index]) >> s.right_shift) & s.mask;
      val |= (chunk << s.left_shift);
    }
  }

  int64_t signed_val = static_cast<int64_t>(val);
  if (sig.decode_plan.is_signed && sig.decode_plan.size < 64) {
    const int shift = 64 - sig.decode_plan.size;
    signed_val = (static_cast<int64_t>(val << shift)) >> shift;
  }

  return (static_cast<double>(signed_val) * sig.decode_plan.factor) + sig.decode_plan.offset;
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
