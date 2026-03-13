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

uint8_t clearIgnoredBits(uint8_t value, uint8_t ignore_mask) {
  return value & static_cast<uint8_t>(~ignore_mask);
}

}  // namespace

void MessageState::init(const uint8_t* new_data, uint8_t data_size, double current_ts) {
  const auto preserved_mask = suppressed_mask;

  size = std::min<uint8_t>(data_size, MAX_CAN_LEN);
  ts = current_ts;
  count = 1;
  freq = 0;
  last_freq_ts = current_ts;

  std::memset(data.data(), 0, MAX_CAN_LEN);
  std::memcpy(data.data(), new_data, size);

  analysis.fill({});
  colors.fill(0);
  for (auto& f : bit_flips) f.fill(0);

  suppressed_mask.fill(0);
  std::copy_n(preserved_mask.begin(), size, suppressed_mask.begin());
  ignore_mask_64.fill(0);
  prev_data_64.fill(0);
  std::memcpy(prev_data_64.data(), new_data, size);
}

void MessageState::update(const uint8_t* new_data, uint8_t data_size, double current_ts, double manual_freq, bool is_seek) {
  if (size != data_size) {
    init(new_data, data_size, current_ts);
    return;
  }

  ts = current_ts;
  count++;
  updateFrequency(current_ts, manual_freq, is_seek);

  // Decay EMA for all bytes. When EMA decays below noise threshold, the byte has gone
  // quiet: halve trend_streak so old history doesn't bias future classifications.
  const float decay = 1.0f - TOGGLE_EMA_ALPHA;
  for (int i = 0; i < size; ++i) {
    auto& a = analysis[i];
    a.toggle_ema *= decay;
    if (a.toggle_ema <= NOISE_EMA_THRESHOLD) {
      if (a.trend_streak != 0) a.trend_streak /= 2;
      if (a.trend_streak == 0) a.pattern = DataPattern::None;
    }
  }

  // Scan in 8-byte blocks: skip unchanged blocks in O(1) via uint64_t XOR
  const int num_blocks = (size + 7) / 8;
  for (int b = 0; b < num_blocks; ++b) {
    const int offset = b * 8;
    const int block_len = std::min(8, size - offset);

    uint64_t cur_64 = 0;
    std::memcpy(&cur_64, new_data + offset, block_len);

    const uint64_t raw_diff = cur_64 ^ prev_data_64[b];
    if (raw_diff != 0) {
      const uint64_t ignored = ignore_mask_64[b];
      const uint64_t changed = raw_diff & ~ignored;
      if (changed != 0) {
        for (int j = 0; j < block_len; ++j) {
          const int shift = j * 8;
          if (static_cast<uint8_t>(changed >> shift) == 0) continue;

          const int idx = offset + j;
          const uint8_t mask = static_cast<uint8_t>(ignored >> shift);
          updateByteActivity(idx,
                             clearIgnoredBits(static_cast<uint8_t>(prev_data_64[b] >> shift), mask),
                             clearIgnoredBits(new_data[idx], mask),
                             current_ts);
        }
      }
      std::memcpy(data.data() + offset, new_data + offset, block_len);
      prev_data_64[b] = cur_64;
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

void MessageState::updateByteActivity(int byte_idx, uint8_t old_byte, uint8_t new_byte, double current_ts) {
  auto& a = analysis[byte_idx];

  // Bit flip counters & timestamps (index 0 = MSB = bit 7 of the byte)
  for (uint8_t bits = old_byte ^ new_byte; bits != 0; bits &= bits - 1) {
    const int bit = 7 - std::countr_zero(bits);
    bit_flips[byte_idx][bit]++;
    a.bit_change_ts[bit] = current_ts;
  }

  // EMA toggle rate (already decayed in update(); add one change sample)
  a.toggle_ema += TOGGLE_EMA_ALPHA;
  a.last_change_ts = current_ts;

  // Trend streak: track consecutive same-direction changes.
  // Only classify as Toggle when not already in an established trend.
  const int delta = static_cast<int>(new_byte) - static_cast<int>(old_byte);
  const bool is_toggle = (delta == -a.last_delta) && (std::abs(a.trend_streak) < TREND_STREAK_THRESHOLD);
  if (is_toggle) {
    a.trend_streak = 0;
  } else if (delta > 0) {
    a.trend_streak = (a.trend_streak > 0) ? std::min<int8_t>(a.trend_streak + 1, TREND_STREAK_MAX) : 1;
  } else if (delta < 0) {
    a.trend_streak = (a.trend_streak < 0) ? std::max<int8_t>(a.trend_streak - 1, -TREND_STREAK_MAX) : -1;
  }

  // Pattern classification
  DataPattern pattern = DataPattern::None;
  if (std::abs(a.trend_streak) >= TREND_STREAK_THRESHOLD) {
    pattern = (a.trend_streak > 0) ? DataPattern::Increasing : DataPattern::Decreasing;
  } else if (is_toggle) {
    pattern = DataPattern::Toggle;
  } else if (a.toggle_ema > NOISE_EMA_THRESHOLD) {
    pattern = DataPattern::RandomlyNoisy;
  }
  a.pattern = pattern;
  a.last_delta = static_cast<int16_t>(delta);
}

void MessageState::applyMask(const std::vector<uint8_t>& dbc_mask) {
  ignore_mask_64.fill(0);
  for (size_t i = 0; i < size; ++i) {
    uint8_t user_mask = (i < dbc_mask.size()) ? dbc_mask[i] : 0;
    uint8_t combined = user_mask | suppressed_mask[i];
    if (combined != 0) {
      ignore_mask_64[i / 8] |= static_cast<uint64_t>(combined) << ((i % 8) * 8);
    }
  }
}

size_t MessageState::muteActiveBits(const std::vector<uint8_t>& dbc_mask) {
  bool modified = false;
  size_t total_suppressed_bits = 0;

  for (size_t i = 0; i < size; ++i) {
    const auto& a = analysis[i];
    uint8_t active_bits = 0;
    for (int bit = 0; bit < 8; ++bit) {
      if (a.bit_change_ts[bit] > 0 && ts - a.bit_change_ts[bit] < kMuteActivityWindowSec) {
        active_bits |= (0x80 >> bit);
      }
    }

    uint8_t old_mask = suppressed_mask[i];
    suppressed_mask[i] |= active_bits;
    if (suppressed_mask[i] != old_mask) modified = true;
    total_suppressed_bits += std::popcount(suppressed_mask[i]);
  }

  if (modified) applyMask(dbc_mask);
  return total_suppressed_bits;
}

void MessageState::unmuteActiveBits(const std::vector<uint8_t>& dbc_mask) {
  suppressed_mask.fill(0);
  applyMask(dbc_mask);
}

void MessageState::updateAllPatternColors(double current_can_sec) {
  for (size_t i = 0; i < size; ++i) {
    const auto& a = analysis[i];
    colors[i] = colorFromDataPattern(a.pattern, current_can_sec, a.last_change_ts, freq);
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
