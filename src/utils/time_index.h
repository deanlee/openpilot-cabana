#include <algorithm>
#include <cstdint>
#include <vector>

template <typename T>
class TimeIndex {
 public:
  // Sync the index with a data container
  void sync(const std::vector<T>& data, uint64_t start_ts, uint64_t end_ts, bool rebuild = false) {
    if (rebuild) indices_.clear();

    if (data.empty()) return;

    // Pre-allocate buckets (1-second intervals)
    size_t expected_size = (end_ts - start_ts) / 1000000000 + 1;
    if (expected_size > indices_.size()) {
      indices_.reserve(expected_size);
    }

    // Resume from last indexed point
    size_t start_from = (rebuild || indices_.empty()) ? 0 : indices_.back();

    for (size_t i = start_from; i < data.size(); ++i) {
      // We use a lambda to get the timestamp from different types
      uint64_t ts = get_timestamp(data[i]);
      size_t sec = (ts - start_ts) / 1000000000;

      while (indices_.size() <= sec) {
        indices_.push_back(i);
      }
    }
  }

  // Returns [min_idx, max_idx] to constrain a search
  std::pair<size_t, size_t> getBounds(uint64_t start_ts, uint64_t search_ts, size_t total_size) const {
    if (indices_.empty() || search_ts <= start_ts) {
      return {0, total_size};
    }

    size_t sec = (search_ts - start_ts) / 1000000000;
    if (sec >= indices_.size()) {
      return {indices_.back(), total_size};
    }

    size_t min_idx = indices_[sec];
    size_t max_idx = (sec + 1 < indices_.size()) ? indices_[sec + 1] : total_size;
    return {min_idx, max_idx};
  }

  void clear() { indices_.clear(); }

 private:
  std::vector<size_t> indices_;

  // Overload this or use a lambda to extract time from T
  static uint64_t get_timestamp(const T& item);
};
