
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
  // Per-byte analysis state for pattern detection and activity tracking
  struct ByteAnalysis {
    std::array<double, 8> bit_change_ts = {};  // Per-bit last-change timestamps (index 0 = MSB)
    double last_change_ts = 0.0;               // Last time any bit in this byte changed
    float toggle_ema = 0.0f;                   // EMA of byte change rate [0,1]
    int16_t last_delta = 0;                    // Previous byte delta for toggle detection
    int8_t trend_streak = 0;                   // Signed saturating streak counter
    DataPattern pattern = DataPattern::None;
  };

  void updateByteActivity(int byte_idx, uint8_t old_byte, uint8_t new_byte, double current_ts);
  void updateFrequency(double current_ts, double manual_freq, bool is_seek);

  static constexpr double kMuteActivityWindowSec = 2.0;

  double last_freq_ts = 0;
  std::array<ByteAnalysis, MAX_CAN_LEN> analysis = {};
  std::array<uint8_t, MAX_CAN_LEN> suppressed_mask = {};       // Per-byte bit suppression (0xFF = all suppressed)
  std::array<uint64_t, 8> prev_data_64 = {};                   // Previous data packed as 8-byte blocks
  std::array<uint64_t, 8> ignore_mask_64 = {};                 // Packed ignore masks per 8-byte block
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
