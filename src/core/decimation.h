// Port of reference/js/decimation.js — QEM decimation (Garland & Heckbert)
// with boundary protection, link-condition and normal-flip guards, crease
// penalty quadrics, and flat-face harvesting past the target.
#pragma once

#include <cstdint>
#include <functional>

#include "core/geometry.h"

namespace core {

inline constexpr double kDefaultHarvestTol = 0.005; // mm; ceil = tol²

Geometry decimate(const Geometry& geometry, int64_t targetTriangles,
                  const std::function<void(double)>& onProgress = nullptr,
                  bool harvestFlat = true,
                  double harvestTol = kDefaultHarvestTol);

} // namespace core
