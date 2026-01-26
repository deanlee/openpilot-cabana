#include "message_state.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "modules/settings/settings.h"
#include "utils/util.h"

namespace {

static constexpr int TOGGLE_DECAY = 40;
static constexpr int TREND_INC = 40;
static constexpr int JITTER_DECAY = 100;
static constexpr int TREND_MAX = 255;

static constexpr int LIMIT_NOISY = 60;
static constexpr int LIMIT_TOGGLE = 100;
static constexpr int LIMIT_TREND = 160;

// static constexpr double ALPHA_DECAY_SECONDS = 1.5;
static constexpr double ENTROPY_THRESHOLD = 0.85;  // Above this, it's considered "Noisy"
static constexpr int MIN_SAMPLES_FOR_ENTROPY = 16;

// Precomputed table
static const std::array<float, 256> ENTROPY_LOOKUP = [] {
  std::array<float, 256> table;
  for (int i = 0; i < 256; ++i) {
    double p = i / 255.0;
    table[i] = (p <= 0.001 || p >= 0.999) ? 0.0f :
               static_cast<float>(-(p * std::log2(p) + (1.0 - p) * std::log2(1.0 - p)));
  }
  return table;
}();

// Calculates Shannon Entropy for a single bit based on the probability 'p'
double calculateBitEntropy(uint32_t highs, uint32_t total) {
  if (total < MIN_SAMPLES_FOR_ENTROPY) return 0.0;

  return ENTROPY_LOOKUP[(highs * 255) / total];
}

}  // namespace

void MessageState::update(const MessageId& msg_id, const uint8_t* new_data, int size,
                          double current_ts, double playback_speed, double manual_freq, bool is_seek) {
  is_active = true;
  ts = current_ts;
  count++;

  // 1. Frequency Management
  updateFrequency(current_ts, manual_freq, is_seek);

  // 2. Data Resizing
  if (dat.size() != static_cast<size_t>(size)) {
    init(new_data, size, current_ts);
    return;
  }

  // 3. Sparse Byte Scanning (Jumps directly to changed bytes)
  const int num_blocks = (size + 7) / 8;
  for (int b = 0; b < num_blocks; ++b) {
    const int offset = b * 8;
    const int bytes_in_block = std::min(8, size - offset);

    uint64_t cur_64 = 0;
    std::memcpy(&cur_64, new_data + offset, bytes_in_block);

    uint64_t diff_64 = (cur_64 ^ last_data_64[b]) & ~ignore_bit_mask[b];

    while (diff_64 != 0) {
      // Hardware-accelerated "jump" to the next changed byte
      int first_bit = __builtin_ctzll(diff_64);
      int byte_offset = first_bit / 8;
      int idx = offset + byte_offset;

      if (byte_offset < bytes_in_block) {
        uint8_t byte_diff = static_cast<uint8_t>((diff_64 >> (byte_offset * 8)) & 0xFF);
        analyzeByteMutation(idx, dat[idx], new_data[idx], byte_diff, current_ts);
        dat[idx] = new_data[idx];
      }

      // Clear the processed byte's bits in the diff mask
      diff_64 &= ~(0xFFULL << (byte_offset * 8));
    }
    last_data_64[b] = cur_64;
  }
}

void MessageState::updateFrequency(double current_ts, double manual_freq, bool is_seek) {
  if (manual_freq > 0) {
    freq = manual_freq;
  } else if (is_seek || last_freq_ts == 0) {
    last_freq_ts = current_ts;
  } else {
    double interval = current_ts - last_freq_ts;

    // Skip interval math if it's too small (jitter)
    if (interval > 0.0001) {
      double instant_freq = 1.0 / interval;
      // Adaptive Filter:
      // High freq (interval < 0.1s) -> Alpha 0.1 (Heavy smoothing)
      // Low freq (interval >= 0.1s) -> Alpha 0.6 (Fast response)
      double alpha = (interval < 0.1) ? 0.1 : 0.6;
      freq = (freq == 0.0) ? instant_freq : (freq * (1.0 - alpha)) + (instant_freq * alpha);
    }
    last_freq_ts = current_ts;
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

  auto* const h_ptr = &bit_high_counts[i][0];
  auto* const f_ptr = &bit_flips[i][0];

  // Use local variables to allow the compiler to use registers instead of
  // hitting the cache 16 times in a row.
  uint8_t nv = new_v;
  uint8_t df = diff;

  h_ptr[7] += (nv >> 0) & 1;
  h_ptr[6] += (nv >> 1) & 1;
  h_ptr[5] += (nv >> 2) & 1;
  h_ptr[4] += (nv >> 3) & 1;
  h_ptr[3] += (nv >> 4) & 1;
  h_ptr[2] += (nv >> 5) & 1;
  h_ptr[1] += (nv >> 6) & 1;
  h_ptr[0] += (nv >> 7) & 1;

  f_ptr[7] += (df >> 0) & 1;
  f_ptr[6] += (df >> 1) & 1;
  f_ptr[5] += (df >> 2) & 1;
  f_ptr[4] += (df >> 3) & 1;
  f_ptr[3] += (df >> 4) & 1;
  f_ptr[2] += (df >> 5) & 1;
  f_ptr[1] += (df >> 6) & 1;
  f_ptr[0] += (df >> 7) & 1;

  // 2. Entropy Analysis (Shannon Entropy)
  double total_entropy = 0.0;
  for (int bit = 0; bit < 8; ++bit) {
    total_entropy += calculateBitEntropy(bit_high_counts[i][bit], count);
  }
  const double avg_entropy = total_entropy / 8.0;

  // 3. Behavior & Trend Detection
  const bool is_toggle = (delta == -s.last_delta) && (delta != 0);
  const bool is_constant_step = (delta == s.last_delta) && (delta != 0);
  const bool same_direction = (delta > 0) == (s.last_delta > 0);

 if (is_constant_step) {
    // Strongest indicator of a counter
    s.trend_weight = std::min(TREND_MAX, s.trend_weight + (TREND_INC * 2));
  } else if (delta != 0 && same_direction) {
    s.trend_weight = std::min(TREND_MAX, s.trend_weight + TREND_INC);
  } else if (is_toggle) {
    // Toggles are distinct from trends; reduce trend weight to allow Toggle classification
    s.trend_weight = std::max(0, s.trend_weight - TOGGLE_DECAY);
  } else {
    s.trend_weight = std::max(0, s.trend_weight - JITTER_DECAY);
  }

  // 4. Classification Hierarchy
  // Logic: Toggle > Trend > Noise > None
  DataPattern new_pattern = DataPattern::None;
  if (is_toggle && s.trend_weight < LIMIT_TOGGLE) {
    new_pattern = DataPattern::Toggle;
  } else if (s.trend_weight > LIMIT_TREND) {
    new_pattern = (delta > 0) ? DataPattern::Increasing : DataPattern::Decreasing;
  } else if (avg_entropy > ENTROPY_THRESHOLD || s.trend_weight > LIMIT_NOISY) {
    new_pattern = DataPattern::RandomlyNoisy;
  }

  // If we found 'None', we keep the previous pattern so the color can
  // fade out smoothly using the 'elapsed' timer.
  if (new_pattern != DataPattern::None) {
    detected_patterns[i] = new_pattern;
  }

  s.last_delta = delta;
  s.last_change_ts = current_ts;
}

void MessageState::updateAllPatternColors(double current_can_sec) {
  if (colors.size() != byte_states.size()) {
    colors.resize(byte_states.size());
  }
  for (size_t i = 0; i < byte_states.size(); ++i) {
    colors[i] = colorFromDataPattern(detected_patterns[i], current_can_sec, byte_states[i].last_change_ts);
  }
}

uint32_t colorFromDataPattern(DataPattern pattern, double current_ts, double last_ts) {
  if (pattern == DataPattern::None) return 0x00000000;

  const double elapsed = std::max(0.0, current_ts - last_ts);
  const double DECAY_TIME = 3.0;

  if (elapsed >= DECAY_TIME) return 0x00000000; // Fully transparent

  // Non-linear decay (Alpha stays higher for longer)
  float intensity = static_cast<float>(std::cos((elapsed / DECAY_TIME) * (1.5707963))); // PI / 2
  uint32_t alpha = static_cast<uint32_t>(200 * intensity);

  struct RGB { uint8_t r, g, b; };
  struct ThemeColors { RGB light, dark; };

  static const ThemeColors palette[] = {
      {{200, 200, 200}, {80, 80, 80}},   // None
      {{46, 204, 113}, {39, 174, 96}},   // Increasing
      {{231, 76, 60}, {192, 57, 43}},    // Decreasing
      {{241, 196, 15}, {243, 156, 18}},  // Toggle
      {{155, 89, 182}, {142, 68, 173}}   // Noisy
  };

  const int index = std::clamp(static_cast<int>(pattern), 0, 4);
  const RGB& rgb = utils::isDarkTheme() ? palette[index].dark : palette[index].light;

  // Manual bit-shift construction: 0xAARRGGBB
  return (alpha << 24) | (rgb.r << 16) | (rgb.g << 8) | rgb.b;
}
