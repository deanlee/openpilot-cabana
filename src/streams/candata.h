#pragma once
#include <QColor>
#include "dbc/dbcmanager.h"

struct CanData {
  CanData() = default;
  CanData(const CanData&) = default;
  CanData& operator=(const CanData&) = default;
  CanData(CanData&&) = default;
  CanData& operator=(CanData&&) = default;
  void update(const MessageId &msg_id, const uint8_t *dat, const int size, double current_sec,
               double playback_speed, const std::vector<uint8_t> &mask, double in_freq = 0);

  double ts = 0.;
  uint32_t count = 0;
  double freq = 0;
  std::vector<uint8_t> dat;
  std::vector<QColor> colors;

  struct ByteLastChange {
    double ts = 0;
    int delta = 0;
    int same_delta_counter = 0;
    bool suppressed = false;
  };
  std::vector<ByteLastChange> last_changes;
  std::vector<std::array<uint32_t, 8>> bit_flip_counts;
  double last_freq_update_ts = 0;
};
