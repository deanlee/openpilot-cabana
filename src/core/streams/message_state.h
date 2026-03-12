
#pragma once

#include <array>
#include <vector>

#include "core/dbc/dbc_message.h"

constexpr int MAX_CAN_LEN = 64;

enum class DataPattern : uint8_t { None = 0, Increasing, Decreasing, Toggle, RandomlyNoisy };

class MessageState {
 public:
  void init(const uint8_t* new_data, uint8_t data_size, double current_ts);
  void update(const uint8_t* new_data, uint8_t data_size, double current_ts, double manual_freq = 0, bool is_seek = false);
  void updateAllPatternColors(double current_ts);
  void applyMask(const std::vector<uint8_t>& dbc_mask);
  size_t muteActiveBits(const std::vector<uint8_t>& dbc_mask);
  void unmuteActiveBits(const std::vector<uint8_t>& dbc_mask);

  double ts = 0.0;     // Latest message timestamp
  double freq = 0.0;   // Message frequency (Hz)
  uint32_t count = 0;  // Total messages received
  uint8_t size = 0;    // Message length in bytes
  bool dirty = false;  // Whether this message has uncommitted changes (for snapshotting)

  std::array<uint8_t, MAX_CAN_LEN> data = {0};  // Raw payload
  std::array<uint32_t, MAX_CAN_LEN> colors = {0};

  std::array<std::array<uint32_t, 8>, MAX_CAN_LEN> bit_flips = {};

 private:
  void updateByteActivity(int byte_index, uint8_t old_val, uint8_t new_val, uint8_t diff, double current_ts);
  void updateFrequency(double current_ts, double manual_freq, bool is_seek);

  static constexpr double kMuteActivityWindowSec = 2.0;

  double last_freq_ts = 0;
  std::array<std::array<double, 8>, MAX_CAN_LEN> last_bit_change_ts = {};
  std::array<float, MAX_CAN_LEN> toggle_ema = {};       // EMA of byte change rate [0,1]
  std::array<int8_t, MAX_CAN_LEN> trend_streak = {};    // Signed saturating streak counter
  std::array<int16_t, MAX_CAN_LEN> last_delta = {};     // Previous byte delta for toggle detection
  std::array<uint8_t, MAX_CAN_LEN> is_suppressed_mask = {0};  // a bitmask per byte (0xFF = all bits suppressed)
  std::array<DataPattern, MAX_CAN_LEN> detected_patterns = {DataPattern::None};
  std::array<uint64_t, 8> last_data_64 = {0};
  std::array<uint64_t, 8> ignore_bit_mask = {0};
};

class MessageSnapshot {
 public:
  MessageSnapshot() = default;
  explicit MessageSnapshot(const MessageState& s) { updateFrom(s); }
  void updateFrom(const MessageState& s);
  void updateActiveState(double now);

  double ts = 0.0;
  double freq = 0.0;
  uint32_t count = 0;
  uint8_t size = 0;
  bool is_active = false;
  std::array<uint8_t, MAX_CAN_LEN> data = {0};
  std::array<uint32_t, MAX_CAN_LEN> colors = {0};
  std::array<std::array<uint32_t, 8>, MAX_CAN_LEN> bit_flips = {{}};
};

uint32_t colorFromDataPattern(DataPattern pattern, double current_ts, double last_ts, double freq);
