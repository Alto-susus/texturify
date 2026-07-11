// Port of reference/js/regularize.js — sliver removal via short-edge collapse
// with edge-length / normal-preservation / link-condition gates, two-tier
// aggressiveness, sharp-edge vertex freezing, and per-gate reject stats.
#pragma once

#include <cstdint>
#include <vector>

#include "core/geometry.h"

namespace core {

struct RegularizeOpts {
  double aspectThreshold = 5;
  double slack = 3.0;
  double aggressiveSlack = 8.0;
  double extremeSliverAspect = 8;
  double maxNormalDeltaCos = -2;        // default cos(15°) applied when < -1
  double aggressiveNormalDeltaCos = -2; // default cos(25°) applied when < -1
  double sharpEdgeCos = -2;             // default cos(30°) applied when < -1
  int maxRounds = 8;
};

struct RegularizeRejectStats {
  int64_t frozen = 0;
  int64_t wingCount = 0;
  int64_t linkCondition = 0;
  int64_t edgeCap = 0;
  int64_t normalChange = 0;
  int64_t degenerate = 0;
  int64_t foldedApex = 0;
};

struct RegularizeResult {
  Geometry geometry;                // positions + recomputed vertex normals
  std::vector<float> excludeWeight; // carried through when input had it
  std::vector<int32_t> faceParentId;
  int64_t collapseCount = 0;
  RegularizeRejectStats rejectStats;
};

RegularizeResult regularizeMesh(const Geometry& geometry,
                                const std::vector<int32_t>& faceParentId,
                                double maxEdgeLength,
                                const RegularizeOpts& opts = {},
                                const std::vector<float>* excludeWeight = nullptr);

} // namespace core
