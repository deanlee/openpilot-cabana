#include "message_state.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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

double calc_freq(const MessageId& msg_id, double current_ts) {
  auto [first, last] = can->eventsInRange(msg_id, std::make_pair(current_ts - 59.0, current_ts));
  const int n = std::distance(first, last);
  if (n <= 1) return 0.0;

  double duration = ((*std::prev(last))->mono_time - (*first)->mono_time) / 1e9;
  return (duration > 1e-9) ? (n - 1) / duration : 0.0;
}

}  // namespace

void MessageState::update(const MessageId& msg_id, const uint8_t* new_data, int size,
                          double current_ts, double playback_speed, double manual_freq) {
  ts = current_ts;
  count++;

  if (manual_freq > 0) {
    freq = manual_freq;
  } else if (std::abs(current_ts - last_freq_ts) >= 1.0) {
    freq = calc_freq(msg_id, ts);
    last_freq_ts = current_ts;
  }

  if (dat.size() != (size_t)size) {
    init(new_data, size, current_ts);
    return;
  }

  // 3. Process changes in 8-byte blocks
  const int num_blocks = (size + 7) / 8;
  const int fade_step = std::max(1, (int)(255.0 / (freq + 1.0) / (2.0 * playback_speed)));

  for (int b = 0; b < num_blocks; ++b) {
    uint64_t cur_64 = 0;
    int bytes_in_block = std::min(8, size - (b * 8));
    std::memcpy(&cur_64, new_data + (b * 8), bytes_in_block);

    uint64_t diff_64 = (cur_64 ^ last_data_64[b]) & ~combined_mask[b];

    const uint8_t* change_ptr = reinterpret_cast<const uint8_t*>(&diff_64);
    const uint8_t* mask_ptr = reinterpret_cast<const uint8_t*>(&combined_mask[b]);
    const uint8_t* old_ptr = reinterpret_cast<const uint8_t*>(&last_data_64[b]);

    for (int i = 0; i < bytes_in_block; ++i) {
      int idx = (b * 8) + i;
      if (change_ptr[i]) {
        handleByteChange(idx, old_ptr[i], new_data[idx], change_ptr[i], current_ts);
      } else if (int alpha = colors[idx].alpha(); alpha > 0) {
        colors[idx].setAlpha(mask_ptr[i] == 0xFF ? 0 : std::max(0, alpha - fade_step));
      }
      dat[idx] = new_data[idx];
    }
    last_data_64[b] = cur_64;
  }
}

void MessageState::init(const uint8_t* new_data, int size, double current_ts) {
  dat.assign(new_data, new_data + size);
  colors.assign(size, QColor(0, 0, 0, 0));
  byte_states.assign(size, {current_ts, 0, 0, false});
  bit_flips.assign(size, {0});
  last_data_64.fill(0);
  std::memcpy(last_data_64.data(), new_data, size);
}

void MessageState::handleByteChange(int i, uint8_t old_val, uint8_t new_val, uint8_t diff, double current_ts) {
  auto& state = byte_states[i];
  const int delta = (int)new_val - (int)old_val;

  bool same_dir = (delta > 0) == (state.last_delta > 0);
  state.trend = std::clamp(state.trend + (same_dir ? 1 : -4), 0, 16);

  const double elapsed = current_ts - state.last_ts;

  // Use the theme colors based on delta
  if ((elapsed * freq > 10.0) || state.trend > 8) {
    colors[i] = getThemeColor(delta > 0 ? CYAN : RED);
  } else {
    // Rapid changes get "blended" into the noise color
    colors[i] = blend(colors[i], getThemeColor(GREYISH_BLUE));
  }

  // Fast bit counting
  for (int bit = 0; bit < 8; ++bit) {
    if ((diff >> bit) & 1) bit_flips[i][7 - bit]++;
  }

  state.last_ts = current_ts;
  state.last_delta = delta;
}
