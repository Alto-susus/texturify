// Port of reference/js/meshValidation.js — mesh quality diagnostics.
//
// Fast checks (open edges, shell count) run automatically after load.
// Expensive checks (intersections, overlaps) are triggered on demand and
// support cooperative abort.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "core/exclusion.h" // AdjacencyData
#include "core/geometry.h"

namespace core {

struct FastDiagnostics {
  int64_t openEdges = 0;
  int64_t nonManifoldEdges = 0;
  int64_t shellCount = 0;
};

// Negligible cost on top of buildAdjacency, which the caller already ran.
FastDiagnostics runFastDiagnostics(const AdjacencyData& adjData,
                                   int64_t triCount);

struct ExpensiveDiagnostics {
  bool aborted = false;
  int64_t intersectingPairs = 0;
  int64_t overlappingPairs = 0;
  // Face indices in first-discovery order (JS Set insertion order).
  std::vector<int32_t> intersectFaces;
  std::vector<int32_t> overlapFaces;
};

// Duplicate triangles (exact float-bit vertex match, winding-agnostic) +
// intersecting pairs (spatial-hash broad phase, SAT narrow phase with
// plane-straddle confirmation). shouldAbort is polled periodically; when it
// returns true the result comes back with aborted = true.
ExpensiveDiagnostics runExpensiveDiagnostics(
    const Geometry& geometry,
    const std::function<bool()>& shouldAbort = nullptr);

struct EdgeHighlightPositions {
  std::vector<float> open;        // 6 floats per open edge segment
  std::vector<float> nonManifold; // 6 floats per non-manifold edge segment
};

// Line segments for the diagnostics overlays, in edge-first-seen order.
EdgeHighlightPositions getEdgePositions(const Geometry& geometry);

// Per-triangle 0-based shell id via BFS on the adjacency graph.
std::vector<uint32_t> getShellAssignments(const AdjacencyData& adjData,
                                          int64_t triCount);

} // namespace core
