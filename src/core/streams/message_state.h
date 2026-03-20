
#pragma once

#include <array>
#include <vector>

#include "core/dbc/dbc_message.h"

constexpr int MAX_CAN_LEN = 64;

enum class DataPattern : uint8_t { None = 0, Increasing, Decreasing, Toggle, RandomlyNoisy };

struct BytePatternInfo {
  DataPattern pattern = DataPattern::None;
  double last_change_ts = 0.0;
};

class MessageState {
 public:
  void init(const uint8_t* new_data, uint8_t data_size, double current_ts);
  void update(const uint8_t* new_data, uint8_t data_size, double current_ts, double manual_freq = 0, bool is_seek = false);
  BytePatternInfo bytePattern(int byte_idx) const;
  const auto& combinedMask() const { return mask_; }
  void setDbcMask(const std::vector<uint8_t>& mask);
  size_t muteActiveBits();
  void unmuteActiveBits();

  double ts = 0.0;     // Latest message timestamp
  double freq = 0.0;   // Message frequency (Hz)
  uint32_t count = 0;  // Total messages received
  uint8_t size = 0;    // Message length in bytes
  bool dirty = false;  // Whether this message has uncommitted changes (for snapshotting)

  std::array<uint8_t, MAX_CAN_LEN> data = {0};  // Raw payload
  std::array<std::array<uint32_t, 8>, MAX_CAN_LEN> bit_flips = {};

 private:
  // Hot per-byte state (16 bytes) — 4 fit per cache line.
  // Cold per-bit timestamps (bit_change_ts_) stored separately.
  struct ByteAnalysis {
    double last_change_ts = 0.0;               // Last time any bit in this byte changed
    float toggle_ema = 0.0f;                   // EMA of byte change rate [0,1]
    int16_t last_delta = 0;                    // Previous byte delta for toggle detection
    int8_t trend_streak = 0;                   // Signed saturating streak counter
    DataPattern pattern = DataPattern::None;
  };
  static_assert(sizeof(ByteAnalysis) == 16);

  void updateByteAnalysis(int byte_idx, uint8_t old_byte, uint8_t new_byte, double current_ts);
  void updateFrequency(double current_ts, double manual_freq, bool is_seek);
  void updateCombinedMask();

  static constexpr double kMuteActivityWindowSec = 2.0;

  double last_freq_ts = 0;
  std::array<ByteAnalysis, MAX_CAN_LEN> analysis = {};
  std::array<std::array<double, 8>, MAX_CAN_LEN> bit_change_ts_ = {};
  std::array<uint8_t, MAX_CAN_LEN> dbc_mask_ = {};
  std::array<uint8_t, MAX_CAN_LEN> suppressed_mask = {};
  std::array<uint8_t, MAX_CAN_LEN> mask_ = {};  // Precomputed dbc_mask_ | suppressed_mask
};

class MessageSnapshot {
 public:
  MessageSnapshot() = default;
  void updateFrom(const MessageState& s);
  void computeColors(double current_sec, bool is_dark_theme);
  void updateActiveState(double now);

  double ts = 0.0;
  double freq = 0.0;
  uint32_t count = 0;
  uint8_t size = 0;
  bool is_active = false;
  std::array<uint8_t, MAX_CAN_LEN> data = {0};
  std::array<uint32_t, MAX_CAN_LEN> colors = {0};
  std::array<std::array<uint32_t, 8>, MAX_CAN_LEN> bit_flips = {{}};
  std::array<uint8_t, MAX_CAN_LEN> mask = {0};

 private:
  std::array<BytePatternInfo, MAX_CAN_LEN> patterns_ = {};
};

uint32_t colorFromDataPattern(DataPattern pattern, double current_ts, double last_ts, double freq, bool is_dark_theme);
