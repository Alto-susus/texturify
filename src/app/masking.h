// Port of main.js surface-masking logic:
//  - updateFaceMask(): per-vertex user-exclusion mask + boundary attributes
//  - computeBoundaryFalloffAttr(): per-vertex falloff ramp near mask edges
//  - computeBoundaryEdges(): boundary edge segments packed for the shader's
//    per-fragment distance queries (2 RGBA32F texels per edge, max 64)
//  - distSqPointToTri() + bfsBrushSelect(): the PrusaSlicer-style circle brush
#pragma once

#include <cstdint>
#include <functional>
#include <unordered_set>
#include <vector>

#include "core/exclusion.h"
#include "core/geometry.h"

namespace app {

struct MaskAttributes {
  std::vector<float> faceMask;         // per-vertex, 1 = textured
  std::vector<float> boundaryFalloff;  // per-vertex, default 1
  std::vector<float> boundaryMaskType; // per-vertex, 0 = user, 1 = angle
  std::vector<float> boundaryEdgeTexels; // edgeCount*8 floats (2 texels/edge)
  int boundaryEdgeCount = 0;
};

struct MaskSettings {
  double bottomAngleLimit = 5;
  double topAngleLimit = 0;
  double boundaryFalloff = 0;
};

// updateFaceMask() + (when computeFalloff) computeBoundaryFalloffAttr and
// computeBoundaryEdges. faceNormalAttr = per-vertex faceNormal (vec3).
// selectionMode inverts the set semantics (Include mode). While a paint
// stroke is active the JS skips the falloff recompute — pass
// computeFalloff=false and reuse the previous arrays.
MaskAttributes computeMaskAttributes(
    const core::Geometry& geo, const std::vector<float>& faceNormalAttr,
    const std::unordered_set<int32_t>& paintedFaces, bool selectionMode,
    const MaskSettings& settings, bool computeFalloff);

// Squared distance from point P to triangle ABC (Voronoi-region method).
double distSqPointToTri(double px, double py, double pz, double ax, double ay,
                        double az, double bx, double by, double bz, double cx,
                        double cy, double cz);

// BFS-along-adjacency circle brush (after PrusaSlicer's TriangleSelector):
// paints triangles overlapping the brush cylinder around hitPt (radius² r2,
// axis viewDir), never crossing back-facing triangles.
void bfsBrushSelect(int32_t seedTriIdx, const core::Vec3& hitPt, double r2,
                    const core::Vec3& viewDir, const core::Geometry& geo,
                    const core::AdjacencyData& adj,
                    const std::function<void(int32_t)>& cb);

} // namespace app
