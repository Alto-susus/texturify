// Port of main.js's cylinder-axis inset panel compute helpers:
// _buildCylinderSilhouette (top-down X-Y rasterization of the mesh) and
// autoFitCylinderAxis (Kasa least-squares circle fit). Pure CPU math — the
// UI layer (ui/cylinder_panel.cpp) owns the GL texture upload and the
// interactive drag/pan/wheel handling.
#pragma once

#include <cstdint>
#include <vector>

#include "core/geometry.h"

namespace app {

struct CylinderSilhouette {
  int width = 0, height = 0;
  std::vector<uint8_t> rgba; // width*height*4, straight alpha
  // World-space anchor: (cxw, cyw) is the world point mapped to the pixel
  // center, `scale` is world-units-to-pixels. Frozen at build time; the UI
  // may pan the *view* independently (see CylinderPanelTransform).
  double cxw = 0, cyw = 0, scale = 1;
};

// Rasterizes the mesh's X-Y projection into a width x height RGBA buffer,
// fit with 50% margin around the AABB (padPx=18 inset), matching
// _buildCylinderSilhouette exactly (same fill rule, same anchor math).
CylinderSilhouette buildCylinderSilhouette(const core::Geometry& geo,
                                           const core::Bounds& bounds,
                                           int width, int height);

// Least-squares circle fit (Kasa method) to vertices of triangles whose face
// normal is roughly perpendicular to the cylinder axis (|n.z| < 0.5), so
// inner bores are excluded and end-caps don't pull the fit.
// faceNormalAttr is the per-vertex replicated face-normal buffer (same
// layout as geo.positions, e.g. app::computeFaceNormals()'s output).
// Returns false (no output written) on degenerate/insufficient input.
bool autoFitCylinderAxis(const core::Geometry& geo,
                         const std::vector<float>& faceNormalAttr,
                         const core::Bounds& bounds, double& outCx,
                         double& outCy, double& outRadius);

} // namespace app
