#include "message_state.h"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>

#include "modules/settings/settings.h"

namespace {

constexpr float TOGGLE_EMA_ALPHA = 0.15f;  // ~6-sample half-life for change rate EMA
constexpr int8_t TREND_STREAK_MAX = 8;     // Saturating streak limit
constexpr int8_t TREND_STREAK_THRESHOLD = 4;  // Consecutive same-direction changes for trend
constexpr float NOISE_EMA_THRESHOLD = 0.55f;  // Above this = noisy

constexpr double FREQ_JITTER_THRESHOLD = 0.0001;  // Intervals below this are considered jitter

}  // namespace

void MessageState::init(const uint8_t* new_data, uint8_t data_size, double current_ts) {
  size = std::min<uint8_t>(data_size, MAX_CAN_LEN);
  ts = current_ts;
  count = 1;
  freq = 0;
  last_freq_ts = current_ts;

  std::memcpy(data.data(), new_data, size);
  std::memset(data.data() + size, 0, MAX_CAN_LEN - size);

  analysis.fill({});
  bit_change_ts_.fill({});
  std::memset(bit_flips.data(), 0, sizeof(bit_flips));

  // Preserve suppressed mask for [0, size); clear the tail
  std::memset(suppressed_mask.data() + size, 0, MAX_CAN_LEN - size);
  updateCombinedMask();
}

void MessageState::update(const uint8_t* new_data, uint8_t data_size, double current_ts, double manual_freq, bool is_seek) {
  if (size != data_size) {
    init(new_data, data_size, current_ts);
    return;
  }

  ts = current_ts;
  count++;
  updateFrequency(current_ts, manual_freq, is_seek);

  const float decay = 1.0f - TOGGLE_EMA_ALPHA;
  for (int i = 0; i < size; ++i) {
    auto& a = analysis[i];
    a.toggle_ema *= decay;

    if (new_data[i] != data[i]) {
      const uint8_t xor_bits = new_data[i] ^ data[i];

      // Always track bit flips for ALL changed bits (never masked)
      auto& bts = bit_change_ts_[i];
      for (uint8_t bits = xor_bits; bits != 0; bits &= bits - 1) {
        const int bit = 7 - std::countr_zero(bits);
        bit_flips[i][bit]++;
        bts[bit] = current_ts;
      }

      // Pattern analysis only for unmasked bits
      const uint8_t diff = xor_bits & ~mask_[i];
      if (diff != 0) {
        updateByteAnalysis(i, data[i] & ~mask_[i], new_data[i] & ~mask_[i], current_ts);
      }
      data[i] = new_data[i];
    } else if (a.toggle_ema <= NOISE_EMA_THRESHOLD) {
      // Idle byte: fade out old classification
      if (a.trend_streak != 0) a.trend_streak /= 2;
      if (a.trend_streak == 0) a.pattern = DataPattern::None;
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
    if (interval > FREQ_JITTER_THRESHOLD) {
      double instant_freq = 1.0 / interval;
      // Adaptive EMA: heavy smoothing for fast msgs, fast response for slow msgs
      double alpha = (interval < 0.1) ? 0.1 : 0.6;
      freq = (freq == 0.0) ? instant_freq : (freq * (1.0 - alpha)) + (instant_freq * alpha);
    }
    if (interval >= 0) {
      last_freq_ts = current_ts;
    }
  }
}

void MessageState::updateCombinedMask() {
  for (int i = 0; i < MAX_CAN_LEN; ++i)
    mask_[i] = dbc_mask_[i] | suppressed_mask[i];
}

void MessageState::updateByteAnalysis(int byte_idx, uint8_t old_byte, uint8_t new_byte, double current_ts) {
  auto& a = analysis[byte_idx];

  a.toggle_ema += TOGGLE_EMA_ALPHA;
  a.last_change_ts = current_ts;

  // Trend streak: detect consecutive same-direction changes vs toggling
  const int delta = static_cast<int>(new_byte) - static_cast<int>(old_byte);
  const bool is_toggle = (delta == -a.last_delta) && (std::abs(a.trend_streak) < TREND_STREAK_THRESHOLD);
  if (is_toggle) {
    a.trend_streak = 0;
  } else if (delta > 0) {
    a.trend_streak = (a.trend_streak > 0) ? std::min<int8_t>(a.trend_streak + 1, TREND_STREAK_MAX) : 1;
  } else if (delta < 0) {
    a.trend_streak = (a.trend_streak < 0) ? std::max<int8_t>(a.trend_streak - 1, -TREND_STREAK_MAX) : -1;
  }

  // Classify pattern
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

void MessageState::setDbcMask(const std::vector<uint8_t>& mask) {
  dbc_mask_.fill(0);
  std::memcpy(dbc_mask_.data(), mask.data(), std::min(mask.size(), static_cast<size_t>(size)));
  updateCombinedMask();
}

size_t MessageState::muteActiveBits() {
  const double cutoff = std::max(0.0, ts - kMuteActivityWindowSec);
  size_t total = 0;

  for (size_t i = 0; i < size; ++i) {
    const auto& bts = bit_change_ts_[i];
    uint8_t active_bits = 0;
    for (int bit = 0; bit < 8; ++bit) {
      if (bts[bit] > cutoff) {
        active_bits |= (0x80 >> bit);
      }
    }
    suppressed_mask[i] |= active_bits;
    total += std::popcount(suppressed_mask[i]);
  }
  updateCombinedMask();
  return total;
}

void MessageState::unmuteActiveBits() {
  suppressed_mask.fill(0);
  updateCombinedMask();
}

BytePatternInfo MessageState::bytePattern(int byte_idx) const {
  const auto& a = analysis[byte_idx];
  return {a.pattern, a.last_change_ts};
}

uint32_t colorFromDataPattern(DataPattern pattern, double current_ts, double last_ts, double freq, bool is_dark_theme) {
  if (pattern == DataPattern::None) return 0x00000000;

  const double elapsed = std::max(0.0, current_ts - last_ts);

  // Adaptive decay: slow messages hold color longer, fast messages fade quickly
  double decay_limit = 1.5;
  if (freq > 0) {
    decay_limit = std::clamp(2.0 / freq, 0.4, 2.5);
  }

  if (elapsed >= decay_limit) return 0x00000000;

  // Quadratic ease-out
  const float t = static_cast<float>(elapsed / decay_limit);
  const float intensity = (1.0f - t) * (1.0f - t);
  const uint32_t alpha = static_cast<uint32_t>(230.0f * intensity);

  // {light, dark} per pattern
  struct RGB { uint8_t r, g, b; };
  static constexpr RGB palette[][2] = {
      {{200, 200, 200}, {80, 80, 80}},   // None  (unreachable due to early exit)
      {{46, 204, 113}, {39, 174, 96}},   // Increasing
      {{231, 76, 60}, {192, 57, 43}},    // Decreasing
      {{241, 196, 15}, {243, 156, 18}},  // Toggle
      {{155, 89, 182}, {142, 68, 173}},  // RandomlyNoisy
  };

  const int index = std::clamp(static_cast<int>(pattern), 0, 4);
  const auto& rgb = palette[index][is_dark_theme];
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
  std::memcpy(bit_flips.data(), s.bit_flips.data(), size * sizeof(bit_flips[0]));
  std::memcpy(mask.data(), s.combinedMask().data(), size);
  for (int i = 0; i < size; ++i) {
    patterns_[i] = s.bytePattern(i);
  }
}

void MessageSnapshot::computeColors(double current_sec, bool is_dark_theme) {
  for (int i = 0; i < size; ++i) {
    colors[i] = colorFromDataPattern(patterns_[i].pattern, current_sec, patterns_[i].last_change_ts, freq, is_dark_theme);
  }
}

void MessageSnapshot::updateActiveState(double now) {
  if (ts <= 0 || ts > now) {
    is_active = false;
    return;
  }

  const double elapsed = now - ts;
  double expected_period = (freq > 0) ? (1.0 / freq) : 2.0;
  // Allow 3.5 missed cycles, clamped to [2s, 10s]
  const double threshold = std::clamp(expected_period * 3.5, 2.0, 10.0);
  is_active = (elapsed < threshold);
}
