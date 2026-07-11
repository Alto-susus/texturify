// Port of reference/js/subdivision.js — edge-based adaptive subdivision.
//
// Subdivides until every edge is ≤ maxEdgeLength (12-pass max), with global
// edge marking (crack-free 0/1/2/3-edge split cases), sharp-edge cluster
// splitting in accurate mode (>30° dihedral), exclusion-weight threading, and
// a triangle safety cap.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "core/geometry.h"

namespace core {

// JS: navigator.deviceMemory >= 8 ? 32M : 16M. Node (golden dumps) → 16M.
inline constexpr int64_t kSubdivSafetyCapLow = 16'000'000;
inline constexpr int64_t kSubdivSafetyCapHigh = 32'000'000;

// Returns the cap the app should use on this machine (≥8 GB RAM → 32M).
int64_t subdivisionSafetyCap();

struct SubdivideResult {
  Geometry geometry;                 // non-indexed positions + normals
  std::vector<float> excludeWeight;  // per-vertex, empty when no faceWeights
  bool safetyCapHit = false;
  std::vector<int32_t> faceParentId; // original-face id per output triangle
};

// onProgress(fraction, triCount, longestEdge)
using SubdivideProgress = std::function<void(double, int64_t, double)>;

// faceWeights: optional per-vertex weights (vertexCount) — 1.0 marks vertices
// of user-excluded triangles. fast=true: preview path (position-only merge).
SubdivideResult subdivide(const Geometry& geometry, double maxEdgeLength,
                          const SubdivideProgress& onProgress = nullptr,
                          const std::vector<float>* faceWeights = nullptr,
                          bool fast = false,
                          int64_t safetyCap = kSubdivSafetyCapLow);

} // namespace core
