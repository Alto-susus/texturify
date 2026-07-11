// Port of reference/js/smartResolution.js — recommended subdivision edge
// length from texture detail + triangle budget, subdivision count simulator,
// and the recommended-max-triangles heuristic.
#pragma once

#include <cstdint>
#include <optional>

#include "core/displacement.h"
#include "core/geometry.h"

namespace core {

struct SmartResolutionDiagnostics {
  double pixelsPerEdge = 0;
  double meanGrad = 0;
  double sharpFrac = 0;
  double pixMm = 0;
  double period_mm = 0;
  double surfaceArea = 0;
  double detailEdge = 0;
  double budgetEdge = 0;
  double estTriangles = 0;
  double triBudget = 0;
  bool budgetClamped = false;
  bool edgeClamped = false;
  int64_t recommendedMaxTri = 0;
};

struct SmartResolutionResult {
  bool ok = false;
  double edge = 0;
  SmartResolutionDiagnostics diagnostics;
};

// Recommended Max Triangles for minimum quality loss (10k slider steps,
// clamped to [10k, 2M]).
int64_t computeRecommendedMaxTri(double pixelsPerEdge, double pixMm,
                                 double surfaceArea, double amplitude);

// Predict subdivide(geometry, edge)'s output triangle count by simulating the
// per-triangle split pattern.
double estimateSubdivisionTriCount(const Geometry& geometry, double edge);

SmartResolutionResult computeSmartResolution(const Geometry& geometry,
                                             const Bounds& bounds,
                                             const DisplacementSettings& settings,
                                             const ImageDataRGBA& texture);

} // namespace core
