#include "message_state.h"

#include <algorithm>
#include <cmath>

#include "abstractstream.h"
#include "settings.h"

namespace {

enum ColorType { GREYISH_BLUE, CYAN, RED };

QColor getThemeColor(ColorType c) {
  constexpr int start_alpha = 128;
  static const QColor theme_colors[] = {
    [GREYISH_BLUE] = QColor(102, 86, 169, start_alpha / 2),
    [CYAN] = QColor(0, 187, 255, start_alpha),
    [RED] = QColor(255, 0, 0, start_alpha),
  };
  return (settings.theme == LIGHT_THEME) ? theme_colors[c] : theme_colors[c].lighter(135);
}

inline QColor blend(const QColor &a, const QColor &b) {
  return QColor((a.red() + b.red()) / 2, (a.green() + b.green()) / 2, 
                (a.blue() + b.blue()) / 2, (a.alpha() + b.alpha()) / 2);
}

double calc_freq(const MessageId &msg_id, double current_ts) {
  auto [first, last] = can->eventsInRange(msg_id, std::make_pair(current_ts - 59.0, current_ts));
  const int n = std::distance(first, last);
  if (n <= 1) return 0.0;

  double duration = ((*std::prev(last))->mono_time - (*first)->mono_time) / 1e9;
  return (duration > 1e-9) ? (n - 1) / duration : 0.0;
}

} // namespace

void MessageState::update(const MessageId &msg_id, const uint8_t *new_data, int size, double current_ts,
                          double playback_speed, const std::vector<uint8_t> &bitmask, double manual_freq) {
  ts = current_ts;
  count++;

  // 1. Update frequency once per second
  if (std::abs(current_ts - last_freq_ts) >= 1.0) {
    last_freq_ts = current_ts;
    freq = (manual_freq != 0) ? manual_freq : calc_freq(msg_id, ts);
  }

  // 2. Structural init if size changes
  if (dat.size() != (size_t)size) {
    init(new_data, size, current_ts);
    return;
  }

  // 3. Process changes
  const float fade_step = 1.0f / (freq + 1.0f) / (2.0f * (float)playback_speed);

  for (int i = 0; i < size; ++i) {
    auto &state = byte_states[i];
    auto &color = colors[i];

    uint8_t mask = state.suppressed ? 0x00 : 0xFF;
    if (i < (int)bitmask.size()) mask &= ~bitmask[i];

    const uint8_t old_val = dat[i] & mask;
    const uint8_t new_val = new_data[i] & mask;

    if (old_val != new_val) {
      handleByteChange(i, old_val, new_val, current_ts);
    } else if (color.alphaF() > 0.0) {
      color.setAlphaF(std::max(0.0f, (float)color.alphaF() - fade_step));
    }

    dat[i] = new_data[i];
  }
}

void MessageState::init(const uint8_t *new_data, int size, double current_ts) {
  dat.assign(new_data, new_data + size);
  colors.assign(size, QColor(0, 0, 0, 0));
  byte_states.assign(size, {current_ts, 0, 0, false});
  bit_flips.assign(size, {0});
}

void MessageState::handleByteChange(int i, uint8_t old_val, uint8_t new_val, double current_ts) {
  auto &state = byte_states[i];
  const int delta = new_val - old_val;

  // Track trend: same direction increases counter, reversal decreases it sharply
  bool same_dir = (delta > 0) == (state.last_delta > 0);
  state.trend = std::clamp(state.trend + (same_dir ? 1 : -4), 0, 16);

  const double elapsed = current_ts - state.last_ts;

  // Highlight logic: Cyan/Red for new trends or slow updates, Greyish Blue for rapid/noisy updates
  if ((elapsed * freq > 10) || state.trend > 8) {
    colors[i] = getThemeColor(new_val > old_val ? CYAN : RED);
  } else {
    colors[i] = blend(colors[i], getThemeColor(GREYISH_BLUE));
  }

  // Bit-level flip counters
  const uint8_t diff = old_val ^ new_val;
  for (int bit = 0; bit < 8; ++bit) {
    if ((diff >> bit) & 1) {
      bit_flips[i][7 - bit]++;
    }
  }

  state.last_ts = current_ts;
  state.last_delta = delta;
}
