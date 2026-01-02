#include "candata.h"

#include <cmath>
#include "settings.h"
#include "abstractstream.h"

namespace {

enum Color { GREYISH_BLUE, CYAN, RED};
QColor getColor(int c) {
  constexpr int start_alpha = 128;
  static const QColor colors[] = {
      [GREYISH_BLUE] = QColor(102, 86, 169, start_alpha / 2),
      [CYAN] = QColor(0, 187, 255, start_alpha),
      [RED] = QColor(255, 0, 0, start_alpha),
  };
  return settings.theme == LIGHT_THEME ? colors[c] : colors[c].lighter(135);
}

inline QColor blend(const QColor &a, const QColor &b) {
  return QColor((a.red() + b.red()) / 2, (a.green() + b.green()) / 2, (a.blue() + b.blue()) / 2, (a.alpha() + b.alpha()) / 2);
}

// Calculate the frequency from the past one minute data
double calc_freq(const MessageId &msg_id, double current_sec) {
  auto [first, last] = can->eventsInRange(msg_id, std::make_pair(current_sec - 59, current_sec));
  int count = std::distance(first, last);
  if (count <= 1) return 0.0;

  double duration = ((*std::prev(last))->mono_time - (*first)->mono_time) / 1e9;
  return duration > std::numeric_limits<double>::epsilon() ? (count - 1) / duration : 0.0;
}

}  // namespace

void CanData::update(const MessageId &msg_id, const uint8_t *can_data, const int size, double current_sec,
                      double playback_speed, const std::vector<uint8_t> &mask, double in_freq) {
  ts = current_sec;
  ++count;

  if (std::abs(current_sec - last_freq_update_ts) >= 1.0) {
    last_freq_update_ts = current_sec;
    freq = (in_freq != 0) ? in_freq : calc_freq(msg_id, ts);
  }

  if (dat.size() != size) {
    dat.assign(can_data, can_data + size);
    colors.assign(size, QColor(0, 0, 0, 0));
    last_changes.resize(size);
    bit_flip_counts.resize(size);
    std::for_each(last_changes.begin(), last_changes.end(), [current_sec](auto &c) { c.ts = current_sec; });
  } else {
    constexpr int periodic_threshold = 10;
    constexpr float fade_time = 2.0;
    const float alpha_delta = 1.0 / (freq + 1) / (fade_time * playback_speed);

    for (int i = 0; i < size; ++i) {
      auto &last_change = last_changes[i];
      auto &color = colors[i];

      uint8_t mask_byte = last_change.suppressed ? 0x00 : 0xFF;
      if (i < mask.size()) mask_byte &= ~(mask[i]);

      const uint8_t last = dat[i] & mask_byte;
      const uint8_t cur = can_data[i] & mask_byte;
      if (last != cur) {
        const int delta = cur - last;
        // Keep track if signal is changing randomly, or mostly moving in the same direction
        last_change.same_delta_counter += std::signbit(delta) == std::signbit(last_change.delta) ? 1 : -4;
        last_change.same_delta_counter = std::clamp(last_change.same_delta_counter, 0, 16);

        const double delta_t = ts - last_change.ts;
        // Mostly moves in the same direction, color based on delta up/down
        if (delta_t * freq > periodic_threshold || last_change.same_delta_counter > 8) {
          // Last change was while ago, choose color based on delta up or down
          color = getColor(cur > last ? CYAN : RED);
        } else {
          // Periodic changes
          color = blend(color, getColor(GREYISH_BLUE));
        }

        // Track bit level changes
        const uint8_t diff = (cur ^ last);
        if (diff) {
          auto &row_bit_flips = bit_flip_counts[i];
          for (int bit = 0; bit < 8; bit++) {
            row_bit_flips[7 - bit] += (diff >> bit) & 1;
          }
        }

        last_change.ts = ts;
        last_change.delta = delta;
      } else if (color.alphaF() > 0.0) {
        colors[i].setAlphaF(std::max(0.0, colors[i].alphaF() - alpha_delta));
      }

      dat[i] = can_data[i];
    }
  }
}
