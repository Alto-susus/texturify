// Port of reference/js/exclusion.js — per-face exclusion masking:
// inter-triangle adjacency with dihedral angles, BFS bucket fill, overlay
// geometry extraction, and per-vertex face weights for subdivision.
#pragma once

#include <cstdint>
#include <vector>

#include "core/geometry.h"

namespace core {

struct AdjEntry {
  int32_t neighbor;
  double angle; // dihedral angle in degrees
};

struct AdjacencyData {
  std::vector<std::vector<AdjEntry>> adjacency; // per-triangle neighbours
  std::vector<float> centroids;   // triCount × 3
  std::vector<float> boundRadii;  // triCount
  std::vector<float> faceNormals; // triCount × 3 (unit)
  int64_t openEdgeCount = 0;
  int64_t nonManifoldEdgeCount = 0;
};

AdjacencyData buildAdjacency(const Geometry& geometry);

// BFS flood fill from seedTriIdx across edges with dihedral ≤ thresholdDeg.
// Returned in visit order (JS Set insertion order); seed first.
std::vector<int32_t> bucketFill(int32_t seedTriIdx, const AdjacencyData& adj,
                                double thresholdDeg);

// Compact geometry containing the triangles where mask[t] != 0 (or the
// complement when invert). Copies normals when present.
Geometry buildExclusionOverlayGeo(const Geometry& geometry,
                                  const std::vector<uint8_t>& faceMask,
                                  bool invert = false);

// Per-non-indexed-vertex exclusion weights (1.0 = excluded). invert = painted
// faces are the *included* ones.
std::vector<float> buildFaceWeights(const Geometry& geometry,
                                    const std::vector<uint8_t>& excludedMask,
                                    bool invert = false);

} // namespace core
