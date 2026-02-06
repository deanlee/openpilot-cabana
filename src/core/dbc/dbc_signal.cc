
#include "dbc_signal.h"

#include <algorithm>
#include <cmath>

#include "utils/util.h"

void dbc::Signal::update() {
  updateMsbLsb(*this);
  if (receiver_name.isEmpty()) {
    receiver_name = DEFAULT_NODE_NAME;
  }
  precision = std::max(utils::num_decimals(factor), utils::num_decimals(offset));

  updateColor();
}

void dbc::Signal::updateColor() {
  // 1. Hue angle: Golden ratio stride in [0, 2π) for maximal separation.
  const float phi_inv = 0.6180339887f;
  const float hue = std::fmod(static_cast<float>(lsb) * phi_inv, 1.0f) * 6.2831853f;

  // 2. Theme-aware lightness and chroma in OKLab space.
  const bool is_dark = utils::isDarkTheme();
  const int scrambled = lsb ^ (lsb >> 2) ^ (lsb >> 5);

  // Lightness: 3 tiers for depth. Dark theme is brighter, light theme is richer.
  static constexpr float L_dark[] = {0.75f, 0.68f, 0.82f};
  static constexpr float L_light[] = {0.58f, 0.50f, 0.65f};
  const float L = is_dark ? L_dark[scrambled % 3] : L_light[scrambled % 3];

  // Chroma: 4 levels from vivid to soft — gives a modern "flat UI" palette.
  static constexpr float C_levels[] = {0.14f, 0.10f, 0.17f, 0.12f};
  const float C = C_levels[(scrambled >> 2) & 3];

  // 3. OKLab → linear sRGB conversion
  //    a = C * cos(hue), b = C * sin(hue)
  const float a = C * std::cos(hue);
  const float b = C * std::sin(hue);

  // OKLab to LMS (approximate inverse)
  const float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
  const float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
  const float s_ = L - 0.0894841775f * a - 1.2914855480f * b;

  const float l3 = l_ * l_ * l_;
  const float m3 = m_ * m_ * m_;
  const float s3 = s_ * s_ * s_;

  // LMS to linear sRGB
  const float r_lin = +4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3;
  const float g_lin = -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3;
  const float b_lin = -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3;

  // Linear sRGB → sRGB gamma (approximate: γ = 2.2)
  auto to_srgb = [](float x) -> int {
    x = std::clamp(x, 0.0f, 1.0f);
    return static_cast<int>(std::pow(x, 1.0f / 2.2f) * 255.0f + 0.5f);
  };

  color = QColor(to_srgb(r_lin), to_srgb(g_lin), to_srgb(b_lin));
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
  return name == other.name && size == other.size && start_bit == other.start_bit && msb == other.msb &&
         lsb == other.lsb && is_signed == other.is_signed && is_little_endian == other.is_little_endian &&
         factor == other.factor && offset == other.offset && min == other.min && max == other.max &&
         comment == other.comment && unit == other.unit && value_table == other.value_table &&
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
