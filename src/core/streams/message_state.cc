#include "message_state.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "abstractstream.h"
#include "modules/settings/settings.h"

namespace {

static constexpr int TOGGLE_DECAY = 40;
static constexpr int TREND_INC = 40;
static constexpr int JITTER_DECAY = 100;
static constexpr int TREND_MAX = 255;

static constexpr int LIMIT_NOISY = 60;
static constexpr int LIMIT_TOGGLE = 100;
static constexpr int LIMIT_TREND = 160;

static constexpr double ALPHA_DECAY_SECONDS = 1.5;
static constexpr double ENTROPY_THRESHOLD = 0.85;  // Above this, it's considered "Noisy"
static constexpr int MIN_SAMPLES_FOR_ENTROPY = 16;

// Calculates Shannon Entropy for a single bit based on the probability 'p'
double calculateBitEntropy(uint32_t set_counts, uint32_t total_samples) {
  if (total_samples < MIN_SAMPLES_FOR_ENTROPY) return 0.0;

  double p = static_cast<double>(set_counts) / total_samples;
  if (p <= 0.001 || p >= 0.999) return 0.0;

  // Shannon Entropy: -p*log2(p) - (1-p)*log2(1-p)
  return -(p * std::log2(p) + (1.0 - p) * std::log2(1.0 - p));
}

double calc_freq(const MessageId& msg_id, double current_ts) {
  auto [first, last] = can->eventsInRange(msg_id, std::make_pair(current_ts - 59.0, current_ts));
  const auto n = std::distance(first, last);
  if (n <= 1) return 0.0;

  const double duration = ((*std::prev(last))->mono_time - (*first)->mono_time) / 1e9;
  return (duration > 1e-9) ? (n - 1) / duration : 0.0;
}

}  // namespace

void MessageState::update(const MessageId& msg_id, const uint8_t* new_data, int size,
                          double current_ts, double playback_speed, double manual_freq) {
  is_active = true;
  ts = current_ts;
  count++;

  // 1. Frequency Management
  if (manual_freq > 0) {
    freq = manual_freq;
  } else if (std::abs(current_ts - last_freq_ts) >= 1.0) {
    freq = calc_freq(msg_id, ts);
    last_freq_ts = current_ts;
  }

  // 2. Data Resizing
  if (dat.size() != static_cast<size_t>(size)) {
    init(new_data, size, current_ts);
    return;
  }

  // 3. Process changes in 8-byte blocks (using bitwise shifts for speed/safety)
  const int num_blocks = (size + 7) / 8;
  for (int b = 0; b < num_blocks; ++b) {
    uint64_t cur_64 = 0;
    const int offset = b * 8;
    const int bytes_in_block = std::min(8, size - offset);
    std::memcpy(&cur_64, new_data + offset, bytes_in_block);

    const uint64_t diff_64 = (cur_64 ^ last_data_64[b]) & ~ignore_bit_mask[b];

    if (diff_64 != 0) {
      for (int i = 0; i < bytes_in_block; ++i) {
        const int idx = offset + i;
        const uint8_t byte_diff = static_cast<uint8_t>((diff_64 >> (i * 8)) & 0xFF);

        if (byte_diff) {
          const uint8_t old_v = static_cast<uint8_t>((last_data_64[b] >> (i * 8)) & 0xFF);
          analyzeByteMutation(idx, old_v, new_data[idx], byte_diff, current_ts);
        }
        dat[idx] = new_data[idx];
      }
    }
    last_data_64[b] = cur_64;
  }
}

void MessageState::init(const uint8_t* new_data, int size, double current_ts) {
  dat.assign(new_data, new_data + size);
  byte_states.assign(size, {current_ts, 0, 0, false});
  bit_flips.assign(size, {0});
  bit_high_counts.assign(size, {0});
  detected_patterns.assign(size, DataPattern::None);
  last_data_64.fill(0);
  const int copy_size = std::min(size, (int)(last_data_64.size() * 8));
  std::memcpy(last_data_64.data(), new_data, copy_size);
}

void MessageState::analyzeByteMutation(int i, uint8_t old_v, uint8_t new_v, uint8_t diff, double current_ts) {
  auto& s = byte_states[i];
  const int delta = static_cast<int>(new_v) - static_cast<int>(old_v);

  // 1. Bit-level Analytics
  for (int bit = 0; bit < 8; ++bit) {
    if ((new_v >> bit) & 1) bit_high_counts[i][7 - bit]++;
    if ((diff >> bit) & 1) bit_flips[i][7 - bit]++;
  }

  // 2. Entropy Analysis (Shannon Entropy)
  double total_entropy = 0.0;
  for (int bit = 0; bit < 8; ++bit) {
    total_entropy += calculateBitEntropy(bit_high_counts[i][bit], count);
  }
  const double avg_entropy = total_entropy / 8.0;

  // 3. Behavior & Trend Detection
  const bool is_toggle = (delta == -s.last_delta) && (delta != 0);
  const bool same_direction = (delta > 0) == (s.last_delta > 0);

  if (is_toggle) {
    s.trend_weight = std::max(0, s.trend_weight - TOGGLE_DECAY);
  } else if (delta != 0 && same_direction) {
    s.trend_weight = std::min(TREND_MAX, s.trend_weight + TREND_INC);
  } else {
    s.trend_weight = std::max(0, s.trend_weight - JITTER_DECAY);
  }

  // 4. Classification Hierarchy
  if (avg_entropy > ENTROPY_THRESHOLD) {
    detected_patterns[i] = DataPattern::RandomlyNoisy;
  } else if (is_toggle && s.trend_weight < LIMIT_TOGGLE) {
    detected_patterns[i] = DataPattern::Toggle;
  } else if (s.trend_weight > LIMIT_TREND) {
    detected_patterns[i] = (delta > 0) ? DataPattern::Increasing : DataPattern::Decreasing;
  } else if (s.trend_weight > LIMIT_NOISY) {
    detected_patterns[i] = DataPattern::RandomlyNoisy;
  } else {
    detected_patterns[i] = DataPattern::None;
  }

  s.last_delta = delta;
  s.last_change_ts = current_ts;
}

QColor MessageState::getPatternColor(int idx, double current_ts) const {
  if (idx >= byte_states.size()) return QColor(0, 0, 0, 0);

  return colorFromDataPattern(detected_patterns[idx], current_ts, byte_states[idx].last_change_ts);
}

void MessageState::updateAllPatternColors(double current_can_sec) {
  if (colors.size() != byte_states.size()) {
    colors.resize(byte_states.size());
  }
  for (size_t i = 0; i < byte_states.size(); ++i) {
    colors[i] = colorFromDataPattern(detected_patterns[i], current_can_sec, byte_states[i].last_change_ts);
  }
}

QColor colorFromDataPattern(DataPattern pattern, double current_ts, double last_ts) {
  const double elapsed = current_ts - last_ts;
  const double decay = 1.0 - (elapsed / ALPHA_DECAY_SECONDS);

  if (decay <= 0) return Qt::transparent;

  struct ThemeColors { QColor light, dark; };
  static const ThemeColors palette[] = {
    {{100, 100, 100}, {180, 180, 180}}, // Static/None (Index 0)
    {{0, 120, 215},   {0, 180, 255}},   // Increasing
    {{200, 0, 0},     {255, 60, 60}},    // Decreasing
    {{190, 140, 0},   {255, 230, 0}},    // Toggle
    {{120, 60, 200},  {170, 100, 255}}   // RandomlyNoisy
  };

  const int index = std::clamp(static_cast<int>(pattern), 0, 4);
  const bool is_light = (settings.theme == LIGHT_THEME);

  QColor color = is_light ? palette[index].light : palette[index].dark;
  color.setAlpha(static_cast<int>(255 * decay));

  return color;
}
