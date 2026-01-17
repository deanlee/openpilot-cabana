#include "series_bounds.h"

void SeriesBounds::addPoint(double y) {
  const size_t idx = count_++;

  if (levels_.empty()) levels_.push_back({});

  double cur_min = y;
  double cur_max = y;

  // Propagate updates up through the levels (Mipmapping)
  for (size_t i = 0; i < levels_.size(); ++i) {
    const size_t level_idx = idx / power_of_branch_[i + 1];

    if (level_idx >= levels_[i].size()) {
      levels_[i].push_back({cur_min, cur_max});
    } else {
      levels_[i][level_idx].combine(cur_min);
      levels_[i][level_idx].combine(cur_max);
    }

    // If we didn't just fill a bucket, parent levels won't change
    if ((idx + 1) % power_of_branch_[i + 1] != 0) break;

    cur_min = levels_[i][level_idx].min;
    cur_max = levels_[i][level_idx].max;

    // Create next level if it doesn't exist yet
    if (i + 1 == levels_.size() && i < 6) {
      levels_.push_back({});
    }
  }
}

BoundsNode SeriesBounds::query(int l, int r, const std::vector<QPointF>& raw) const {
  BoundsNode result;
  if (l > r || r >= (int)raw.size()) return result;

  int curr = l;
  while (curr <= r) {
    int best_lvl = -1;
    int best_step = 1;

    // Find highest level bucket that fits entirely within [curr, r]
    for (int lvl = (int)levels_.size() - 1; lvl >= 0; --lvl) {
      int step = (int)power_of_branch_[lvl + 1];
      if (curr % step == 0 && curr + step - 1 <= r) {
        best_lvl = lvl;
        best_step = step;
        break;
      }
    }

    if (best_lvl != -1) {
      result.combine(levels_[best_lvl][curr / best_step]);
      curr += best_step;
    } else {
      // Leaf/Raw data fallback
      result.combine(raw[curr].y());
      curr++;
    }
  }
  return result;
}

void SeriesBounds::clear() {
  levels_.clear();
  count_ = 0;
}
