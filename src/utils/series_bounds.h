#pragma once
#include <QPointF>
#include <algorithm>
#include <limits>
#include <vector>

struct BoundsNode {
  double min = std::numeric_limits<double>::max();
  double max = std::numeric_limits<double>::lowest();

  inline void combine(double val) {
    if (val < min) min = val;
    if (val > max) max = val;
  }

  inline void combine(const BoundsNode& other) {
    if (other.min < min) min = other.min;
    if (other.max > max) max = other.max;
  }
};

/**
 * @brief SeriesBounds provides O(log N) min/max queries for a 1D signal.
 * It uses a hierarchical mipmap structure (branching factor of 8) to ensure
 * instant Y-axis scaling even on very long CAN logs.
 */
class SeriesBounds {
 public:
  static constexpr int BRANCH_FACTOR = 8;

  void addPoint(double y);
  BoundsNode query(int l, int r, const std::vector<QPointF>& raw) const;
  void clear();

 private:
  std::vector<std::vector<BoundsNode>> levels_;
  size_t count_ = 0;
  // Precomputed powers of BRANCH_FACTOR for speed
  size_t power_of_branch_[8] = {1, 8, 64, 512, 4096, 32768, 262144, 2097152};
};
