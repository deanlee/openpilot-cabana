#include "segment_tree.h"

#include <limits>

void SegmentTree::build(const std::vector<QPointF>& arr) {
  size = arr.size();
  tree.resize(4 * size);  // size of the tree is 4 times the size of the array
  if (size > 0) {
    build_tree(arr, 1, 0, size - 1);
  }
}

void SegmentTree::build_tree(const std::vector<QPointF>& arr, int n, int left, int right) {
  if (left == right) {
    const double y = arr[left].y();
    tree[n] = {y, y};
  } else {
    const int mid = (left + right) >> 1;
    build_tree(arr, 2 * n, left, mid);
    build_tree(arr, 2 * n + 1, mid + 1, right);
    tree[n] = {std::min(tree[2 * n].first, tree[2 * n + 1].first),
               std::max(tree[2 * n].second, tree[2 * n + 1].second)};
  }
}

std::pair<double, double> SegmentTree::get_minmax(int n, int left, int right, int range_left, int range_right) const {
  if (range_left > right || range_right < left)
    return {std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest()};
  if (range_left <= left && range_right >= right) return tree[n];
  int mid = (left + right) >> 1;
  auto l = get_minmax(2 * n, left, mid, range_left, range_right);
  auto r = get_minmax(2 * n + 1, mid + 1, right, range_left, range_right);
  return {std::min(l.first, r.first), std::max(l.second, r.second)};
}
