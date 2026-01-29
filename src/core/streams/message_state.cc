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

// Precomputed Shannon Entropy Table: H(p) = -p*log2(p) - (1-p)*log2(1-p)
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
double getEntropy(uint32_t highs, uint32_t total) {
  if (total < MIN_SAMPLES_FOR_ENTROPY) return 0.0;

  int index = std::clamp(static_cast<int>((static_cast<float>(highs) / total) * 255.0f), 0, 255);
  return ENTROPY_LOOKUP[index];
}

}  // namespace

void MessageState::init(const uint8_t* new_data, int data_size, double current_ts) {
  size = std::min<int>(data_size, MAX_CAN_LEN);

  ts = current_ts;
  // Wipe arrays for the new message length
  std::memset(data.data(), 0, MAX_CAN_LEN);
  std::memcpy(data.data(), new_data, size);

  last_change_ts.fill(current_ts);
  last_delta.fill(0);
  trend_weight.fill(0);
  colors.fill(0);
  detected_patterns.fill(DataPattern::None);

  for (auto& f : bit_flips) f.fill(0);
  for (auto& h : bit_high_counts) h.fill(0);

  last_data_64.fill(0);
  std::memcpy(last_data_64.data(), new_data, size);
}

void MessageState::update(const uint8_t* new_data, int data_size,
                          double current_ts, double manual_freq, bool is_seek) {
  ts = current_ts;
  count++;

  updateFrequency(current_ts, manual_freq, is_seek);

  if (size != data_size) {
    init(new_data, data_size, current_ts);
    return;
  }

  const int num_blocks = (size + 7) / 8;
  for (int b = 0; b < num_blocks; ++b) {
    const int offset = b * 8;
    const int block_len = std::min(8, size - offset);

    uint64_t cur_64 = 0;
    std::memcpy(&cur_64, new_data + offset, block_len);

    uint64_t raw_diff_64 = (cur_64 ^ last_data_64[b]);
    if (raw_diff_64 != 0) {
      uint64_t analysis_diff_64 = raw_diff_64 & ~ignore_bit_mask[b];
      while (analysis_diff_64 != 0) {
        // Find first byte with an unmasked change
        int first_bit = __builtin_ctzll(analysis_diff_64);
        int byte_in_block = first_bit / 8;
        int global_idx = offset + byte_in_block;

        // Extract byte-level values for specific mutation analysis
        uint8_t byte_diff = static_cast<uint8_t>((analysis_diff_64 >> (byte_in_block * 8)) & 0xFF);
        uint8_t old_byte = static_cast<uint8_t>((last_data_64[b] >> (byte_in_block * 8)) & 0xFF);
        uint8_t new_byte = new_data[global_idx];

        analyzeByteMutation(global_idx, old_byte, new_byte, byte_diff, current_ts);

        // Clear the entire byte from the analysis mask to find the next changed byte
        analysis_diff_64 &= ~(0xFFULL << (byte_in_block * 8));
      }
      std::memcpy(data.data() + offset, new_data + offset, block_len);
      last_data_64[b] = cur_64;
    }
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

void MessageState::analyzeByteMutation(int i, uint8_t old_v, uint8_t new_v, uint8_t diff, double current_ts) {
  const int delta = static_cast<int>(new_v) - static_cast<int>(old_v);

  // 1. Bit Stats (Now using SoA arrays directly)
  for (int bit = 0; bit < 8; ++bit) {
    bit_high_counts[i][bit] += (new_v >> (7 - bit)) & 1;
    bit_flips[i][bit] += (diff >> (7 - bit)) & 1;
  }

  // 2. Entropy
  float total_entropy = 0.0f;
  for (int bit = 0; bit < 8; ++bit) {
    total_entropy += getEntropy(bit_high_counts[i][bit], count);
  }
  const float avg_entropy = total_entropy / 8.0;

  // 3. Trends (Using SoA arrays last_delta[i] and trend_weight[i])
  const bool is_toggle = (delta == -last_delta[i]) && (delta != 0);
  const bool is_constant_step = (delta == last_delta[i]) && (delta != 0);
  const bool same_direction = (delta > 0) == (last_delta[i] > 0);

  int& weight = trend_weight[i];
  if (is_constant_step) {
    weight = std::min(TREND_MAX, weight + (TREND_INC * 2));
  } else if (delta != 0 && same_direction) {
    weight = std::min(TREND_MAX, weight + TREND_INC);
  } else if (is_toggle) {
    weight = std::max(0, weight - TOGGLE_DECAY);
  } else {
    weight = std::max(0, weight - JITTER_DECAY);
  }

  // 4. Pattern logic
  DataPattern new_p = DataPattern::None;
  if (is_toggle && weight < LIMIT_TOGGLE) {
    new_p = DataPattern::Toggle;
  } else if (weight > LIMIT_TREND) {
    new_p = (delta > 0) ? DataPattern::Increasing : DataPattern::Decreasing;
  } else if (avg_entropy > ENTROPY_THRESHOLD || weight > LIMIT_NOISY) {
    new_p = DataPattern::RandomlyNoisy;
  }

  if (new_p != DataPattern::None) {
    detected_patterns[i] = new_p;
  }

  last_delta[i] = delta;
  last_change_ts[i] = current_ts;
}

void MessageState::updateAllPatternColors(double current_can_sec) {
  for (size_t i = 0; i < size; ++i) {
    colors[i] = colorFromDataPattern(detected_patterns[i], current_can_sec, last_change_ts[i], freq);
  }
}

void MessageState::applyMask(const std::vector<uint8_t>& mask) {
  ignore_bit_mask.fill(0);

  for (size_t i = 0; i < size; ++i) {
    uint8_t m = 0;
    if (is_suppressed[i]) {
      m = 0xFF;
    } else if (i < mask.size()) {
      m = mask[i];
    }

    if (m != 0) {
      ignore_bit_mask[i / 8] |= (static_cast<uint64_t>(m) << ((i % 8) * 8));
      if (m == 0xFF) {
        bit_flips[i].fill(0);
        bit_high_counts[i].fill(0);
      }
    }
  }
}

size_t MessageState::muteActiveBits(const std::vector<uint8_t>& mask) {
  bool modified = false;
  size_t cnt = 0;
  for (size_t i = 0; i < size; ++i) {
    if (!is_suppressed[i] && (ts - last_change_ts[i] < 2.0)) {
      is_suppressed[i] = 1;  // Mark as suppressed
      modified = true;
    }
    cnt += is_suppressed[i];
  }
  if (modified) {
    applyMask(mask);
  }
  return cnt;
}

void MessageState::unmuteActiveBits(const std::vector<uint8_t>& mask) {
  is_suppressed.fill(0);
  // Refresh the mask (this will re-allow highlights for these bits)
  applyMask(mask);
}

uint32_t colorFromDataPattern(DataPattern pattern, double current_ts, double last_ts, double freq) {
  if (pattern == DataPattern::None) return 0x00000000;

    const double elapsed = std::max(0.0, current_ts - last_ts);

  // Adaptive Decay Limit
  // If a message comes in at 1Hz, we want the color to last ~2s so it doesn't blink.
  // If it comes in at 100Hz, we want it to fade in 0.5s to show rapid changes.
  double decay_limit = 1.5;
  if (freq > 0) {
    decay_limit = std::clamp(2.0 / freq, 0.4, 2.5);
  }

  if (elapsed >= decay_limit) return 0x00000000;

  // We want the alpha to hit near-zero at the decay_limit.
  // Using tau = limit / 3.0 ensures e^-3 (~0.05) at the boundary.
  float tau = static_cast<float>(decay_limit / 3.0);
  float intensity = std::exp(-static_cast<float>(elapsed) / tau);

  // Apply a slight "boost" to the initial flash
  uint32_t alpha = static_cast<uint32_t>(230 * intensity);

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

// MessageSnapshot

void MessageSnapshot::updateFrom(const MessageState& s) {
  ts = s.ts;
  freq = s.freq;
  count = s.count;
  size = s.size;
  is_active = true;

  std::memcpy(data.data(), s.data.data(), size);
  std::memcpy(colors.data(), s.colors.data(), size * sizeof(uint32_t));
  std::memcpy(bit_flips.data(), s.bit_flips.data(), size * sizeof(bit_flips[0]));
}

void MessageSnapshot::updateActiveState(double now) {
  // If never received or timestamp is in the future (during seek), inactive.
  if (ts <= 0 || ts > now) {
    is_active = false;
    return;
  }

  const double elapsed = now - ts;
  // Expected gap between messages. Default to 2s if freq is 0.
  double expected_period = (freq > 0) ? (1.0 / freq) : 2.0;
  // Threshold: Allow 3.5 missed cycles.
  // Clamp between 2s (fast msgs) and 10s (slow heartbeats).
  const double threshold = std::clamp(expected_period * 3.5, 2.0, 10.0);
  is_active = (elapsed < threshold);
}
