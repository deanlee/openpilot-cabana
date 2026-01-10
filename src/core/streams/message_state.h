
#pragma once

#include <QColor>
#include <array>
#include <vector>

#include "core/dbc/dbc_message.h"

enum class DataPattern : uint8_t {
  None = 0,
  Increasing,
  Decreasing,
  Toggle,
  RandomlyNoisy
};

struct ByteState {
  double last_change_ts = 0;  // Timestamp of last change
  int last_delta = 0;        // Direction/magnitude of last change
  int trend_weight = 0;             // Directional consistency counter
  bool is_suppressed = false;   // User-defined noise filter
};

class MessageState {
 public:
  void init(const uint8_t* new_data, int size, double current_ts);
  void update(const MessageId& msg_id, const uint8_t* new_data, int size,
              double current_ts, double playback_speed, double manual_freq = 0);
  QColor getPatternColor(int idx, double current_ts) const;
  void updateAllPatternColors(double current_ts);

  double ts = 0.0;     // Latest message timestamp
  double freq = 0.0;   // Message frequency (Hz)
  uint32_t count = 0;  // Total messages received
  bool is_active = false;

  std::array<uint64_t, 8> last_data_64 = {0};
  std::array<uint64_t, 8> ignore_bit_mask = {0};

  std::vector<uint8_t> dat;                        // Raw payload
  std::vector<ByteState> byte_states;              // Per-byte activity tracking
  mutable std::vector<QColor> colors;
  std::vector<std::array<uint32_t, 8>> bit_flips;  // Cumulative bit toggle counts
  std::vector<std::array<uint32_t, 8>> bit_high_counts;
  std::vector<DataPattern> detected_patterns;

 private:
  void analyzeByteMutation(int i, uint8_t old_val, uint8_t new_val, uint8_t diff, double current_ts);
  double last_freq_ts = 0;
};

QColor colorFromDataPattern(DataPattern pattern, double current_ts, double last_ts);
