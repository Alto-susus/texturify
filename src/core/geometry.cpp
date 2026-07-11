#include "core/geometry.h"

namespace core {

Bounds computeBounds(const Geometry& g) {
  Bounds b;
  const auto& p = g.positions;
  if (p.empty()) {
    b.min = {0, 0, 0};
    b.max = {0, 0, 0};
  } else {
    b.min = {p[0], p[1], p[2]};
    b.max = b.min;
    for (size_t i = 0; i < p.size(); i += 3) {
      if (p[i] < b.min.x) b.min.x = p[i];
      if (p[i] > b.max.x) b.max.x = p[i];
      if (p[i + 1] < b.min.y) b.min.y = p[i + 1];
      if (p[i + 1] > b.max.y) b.max.y = p[i + 1];
      if (p[i + 2] < b.min.z) b.min.z = p[i + 2];
      if (p[i + 2] > b.max.z) b.max.z = p[i + 2];
    }
  }
  b.size = b.max - b.min;
  b.center = (b.min + b.max) * 0.5;
  return b;
}

} // namespace core
