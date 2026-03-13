#include "message_state.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

#include "modules/settings/settings.h"
#include "utils/util.h"

namespace {

constexpr float TOGGLE_EMA_ALPHA = 0.15f;  // ~6-sample half-life for change rate EMA
constexpr int8_t TREND_STREAK_MAX = 8;     // Saturating streak limit
constexpr int8_t TREND_STREAK_THRESHOLD = 4;  // Consecutive same-direction changes for trend
constexpr float NOISE_EMA_THRESHOLD = 0.55f;  // Above this = noisy

constexpr double FREQ_JITTER_THRESHOLD = 0.0001;  // Intervals below this are considered jitter

uint8_t applyByteMask(uint8_t value, uint8_t ignore_mask) {
  return value & static_cast<uint8_t>(~ignore_mask);
}

}  // namespace

void MessageState::init(const uint8_t* new_data, uint8_t data_size, double current_ts) {
  const auto preserved_suppressed_mask = is_suppressed_mask;

  size = std::min<uint8_t>(data_size, MAX_CAN_LEN);
  ts = current_ts;
  count = 1;
  freq = 0;
  last_freq_ts = current_ts;

  // Wipe arrays for the new message length
  std::memset(data.data(), 0, MAX_CAN_LEN);
  std::memcpy(data.data(), new_data, size);

  // Bit-level activity initialization — 0.0 means "never changed"
  for (auto& byte_ts : last_bit_change_ts) byte_ts.fill(0.0);

  toggle_ema.fill(0.0f);
  trend_streak.fill(0);
  last_delta.fill(0);
  colors.fill(0);
  detected_patterns.fill(DataPattern::None);

  for (auto& f : bit_flips) f.fill(0);

  is_suppressed_mask.fill(0);
  std::copy_n(preserved_suppressed_mask.begin(), size, is_suppressed_mask.begin());
  ignore_bit_mask.fill(0);
  last_data_64.fill(0);
  std::memcpy(last_data_64.data(), new_data, size);
}

void MessageState::update(const uint8_t* new_data, uint8_t data_size, double current_ts, double manual_freq, bool is_seek) {
  if (size != data_size) {
    init(new_data, data_size, current_ts);
    return;
  }

  ts = current_ts;
  count++;
  updateFrequency(current_ts, manual_freq, is_seek);

  // Decay EMA for all bytes (changed bytes get corrected in updateByteActivity)
  const float decay = 1.0f - TOGGLE_EMA_ALPHA;
  for (int i = 0; i < size; ++i) {
    toggle_ema[i] *= decay;
  }

  const int num_blocks = (size + 7) / 8;
  for (int b = 0; b < num_blocks; ++b) {
    const int offset = b * 8;
    const int block_len = std::min(8, size - offset);

    uint64_t cur_64 = 0;
    std::memcpy(&cur_64, new_data + offset, block_len);

    uint64_t raw_diff_64 = (cur_64 ^ last_data_64[b]);
    if (raw_diff_64 != 0) {
      const uint64_t ignored_bits_64 = ignore_bit_mask[b];
      uint64_t analysis_diff_64 = raw_diff_64 & ~ignored_bits_64;
      while (analysis_diff_64 != 0) {
        int first_bit = std::countr_zero(analysis_diff_64);
        int byte_in_block = first_bit / 8;
        int global_idx = offset + byte_in_block;

        uint8_t byte_diff = static_cast<uint8_t>((analysis_diff_64 >> (byte_in_block * 8)) & 0xFF);
        uint8_t old_byte = static_cast<uint8_t>((last_data_64[b] >> (byte_in_block * 8)) & 0xFF);
        uint8_t new_byte = new_data[global_idx];
        uint8_t ignored_byte_mask = static_cast<uint8_t>((ignored_bits_64 >> (byte_in_block * 8)) & 0xFF);

        updateByteActivity(global_idx, applyByteMask(old_byte, ignored_byte_mask),
                           applyByteMask(new_byte, ignored_byte_mask), byte_diff, current_ts);

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
    if (interval > FREQ_JITTER_THRESHOLD) {
      double instant_freq = 1.0 / interval;
      // Adaptive Filter:
      // High freq (interval < 0.1s) -> Alpha 0.1 (Heavy smoothing)
      // Low freq (interval >= 0.1s) -> Alpha 0.6 (Fast response)
      double alpha = (interval < 0.1) ? 0.1 : 0.6;
      freq = (freq == 0.0) ? instant_freq : (freq * (1.0 - alpha)) + (instant_freq * alpha);
    }
    // Don't regress last_freq_ts on out-of-order timestamps
    if (interval >= 0) {
      last_freq_ts = current_ts;
    }
  }
}

void MessageState::updateByteActivity(int i, uint8_t old_v, uint8_t new_v, uint8_t diff, double current_ts) {
  // 1. Bit stats & timestamps (MSB-first: index 0 is bit 7)
  uint8_t bit_mask = 0x80;
  for (int bit = 0; bit < 8; ++bit) {
    if (diff & bit_mask) { bit_flips[i][bit]++; last_bit_change_ts[i][bit] = current_ts; }
    bit_mask >>= 1;
  }

  // 2. EMA toggle rate — already decayed in update(), add change contribution
  toggle_ema[i] += TOGGLE_EMA_ALPHA;  // Undo decay and add 1.0 sample: decay*old + alpha*1.0

  // 3. Trend streak & toggle detection
  const int delta = static_cast<int>(new_v) - static_cast<int>(old_v);
  const bool is_toggle = (delta != 0) && (delta == -last_delta[i]);

  int8_t& streak = trend_streak[i];
  if (is_toggle) {
    streak = 0;  // alternating delta breaks any trend
  } else if (delta > 0) {
    streak = (streak > 0) ? std::min<int8_t>(streak + 1, TREND_STREAK_MAX) : 1;
  } else if (delta < 0) {
    streak = (streak < 0) ? std::max<int8_t>(streak - 1, -TREND_STREAK_MAX) : -1;
  }

  // 4. Pattern classification — always reassign to prevent stale patterns
  DataPattern new_p = DataPattern::None;
  if (std::abs(streak) >= TREND_STREAK_THRESHOLD) {
    new_p = (streak > 0) ? DataPattern::Increasing : DataPattern::Decreasing;
  } else if (is_toggle) {
    new_p = DataPattern::Toggle;
  } else if (toggle_ema[i] > NOISE_EMA_THRESHOLD) {
    new_p = DataPattern::RandomlyNoisy;
  }
  detected_patterns[i] = new_p;
  last_delta[i] = static_cast<int16_t>(delta);
}

void MessageState::applyMask(const std::vector<uint8_t>& dbc_mask) {
  ignore_bit_mask.fill(0);

  for (size_t i = 0; i < size; ++i) {
    // Combine the permanent mask with the bit-level suppression mask
    uint8_t user_mask = (i < dbc_mask.size()) ? dbc_mask[i] : 0;
    uint8_t combined_mask = user_mask | is_suppressed_mask[i];

    if (combined_mask != 0) {
      ignore_bit_mask[i / 8] |= (static_cast<uint64_t>(combined_mask) << ((i % 8) * 8));
    }
  }
}

size_t MessageState::muteActiveBits(const std::vector<uint8_t>& dbc_mask) {
  bool modified = false;
  size_t total_suppressed_bits = 0;

  for (size_t i = 0; i < size; ++i) {
    uint8_t currently_active_bits = 0;
    for (int bit = 0; bit < 8; ++bit) {
      // Check if this specific bit actually flipped within the last 2 seconds
      if (last_bit_change_ts[i][bit] > 0 && ts - last_bit_change_ts[i][bit] < kMuteActivityWindowSec) {
        currently_active_bits |= (0x80 >> bit);
      }
    }

    // Add newly active bits to the suppression mask
    uint8_t old_mask = is_suppressed_mask[i];
    is_suppressed_mask[i] |= currently_active_bits;

    if (is_suppressed_mask[i] != old_mask) {
      modified = true;
    }

    // Count total 1s in the mask for UI reporting
    total_suppressed_bits += std::popcount(is_suppressed_mask[i]);
  }

  if (modified) {
    applyMask(dbc_mask);
  }
  return total_suppressed_bits;
}

void MessageState::unmuteActiveBits(const std::vector<uint8_t>& dbc_mask) {
  is_suppressed_mask.fill(0);
  applyMask(dbc_mask);
}

void MessageState::updateAllPatternColors(double current_can_sec) {
  for (size_t i = 0; i < size; ++i) {
    // We use the "most recent bit change" in the byte to drive the byte-level color fade
    double latest_bit_ts = 0;
    for (int bit = 0; bit < 8; ++bit) {
      latest_bit_ts = std::max(latest_bit_ts, last_bit_change_ts[i][bit]);
    }
    colors[i] = colorFromDataPattern(detected_patterns[i], current_can_sec, latest_bit_ts, freq);
  }
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

  struct RGB {
    uint8_t r, g, b;
  };
  struct ThemeColors {
    RGB light, dark;
  };

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
