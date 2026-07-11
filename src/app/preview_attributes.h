// Port of main.js preview-geometry attribute helpers: addFaceNormals,
// addSmoothNormals, and the no-exclusion defaults from updateFaceMask()
// (faceMask / boundaryFalloffAttr / boundaryMaskTypeAttr all 1.0).
// The preview shader reads 0.0 for missing attributes, which would make
// totalMask = 0 and render the whole model as masked.
#pragma once

#include <vector>

#include "core/geometry.h"

namespace app {

// addFaceNormals(): true per-triangle normals from the edge cross product,
// replicated to each of the triangle's 3 vertices.
std::vector<float> computeFaceNormals(const core::Geometry& geo);

// addSmoothNormals(): area-weighted average of the buffer normals over each
// unique quantized (1e4) position, so displacement is watertight.
std::vector<float> computeSmoothNormals(const core::Geometry& geo);

struct PreviewAttributeData {
  std::vector<float> faceNormal;       // vec3
  std::vector<float> smoothNormal;     // vec3
  std::vector<float> faceMask;         // 1.0 = textured
  std::vector<float> boundaryFalloff;  // 1.0 = no falloff
  std::vector<float> boundaryMaskType; // 1.0 = angle mask
};

// updateFaceMask() fast path: no user exclusions, no boundary falloff.
PreviewAttributeData buildDefaultPreviewAttributes(const core::Geometry& geo);

} // namespace app
