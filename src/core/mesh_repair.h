// Port of reference/js/meshRepair.js — T-junction resolution for the export
// mesh, plus the cheap edge-defect / area-sliver counters used to verify it.
#pragma once

#include <cstdint>

#include "core/geometry.h"

namespace core {

struct EdgeDefects {
  int64_t open = 0;
  int64_t nonManifold = 0;
  int64_t tris = 0;
};

// Count open (1-face) and non-manifold (3+-face) edges, welding at grid Q.
EdgeDefects countEdgeDefects(const Geometry& geometry, double Q = 1e4);

// Triangles a slicer / our importer would delete as degenerate (area² < 1e-24).
int64_t countAreaSlivers(const Geometry& geometry);

struct ResolveTJunctionOpts {
  double weldQuant = 1e4;
  double onSegTol = 0.02; // mm
  int maxIters = 16;
};

// Snap-weld onto the export grid, drop degenerates/needles, and split boundary
// edges at on-segment boundary vertices until the mesh reads watertight.
Geometry resolveTJunctions(const Geometry& geometry,
                           const ResolveTJunctionOpts& opts = {});

} // namespace core
