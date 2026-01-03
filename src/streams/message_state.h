
#pragma once

#include <QColor>
#include <array>
#include <vector>

#include "dbc/dbc.h"

struct ByteState {
  double last_ts = 0;       // Timestamp of last change
  int last_delta = 0;       // Direction/magnitude of last change
  int trend = 0;            // Directional consistency counter
  bool suppressed = false;  // User-defined noise filter
};

class MessageState {
 public:
  void update(const MessageId& msg_id, const uint8_t* new_data, int size, double current_ts,
              double playback_speed, const std::vector<uint8_t>& bitmask, double manual_freq = 0);

  double ts = 0.0;     // Latest message timestamp
  double freq = 0.0;   // Message frequency (Hz)
  uint32_t count = 0;  // Total messages received

  std::vector<uint8_t> dat;                        // Raw payload
  std::vector<QColor> colors;                      // UI visualization colors
  std::vector<ByteState> byte_states;              // Per-byte activity tracking
  std::vector<std::array<uint32_t, 8>> bit_flips;  // Cumulative bit toggle counts

 private:
  void init(const uint8_t* new_data, int size, double current_ts);
  void handleByteChange(int i, uint8_t old_val, uint8_t new_val, double current_ts);
  double last_freq_ts = 0;
};
