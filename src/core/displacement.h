// Port of reference/js/displacement.js — CPU displacement baking.
//
// For each vertex: compute UV (same math as the GLSL preview), bilinear-sample
// the greyscale height map, move the vertex along its per-unique-position
// smooth normal by (grey [− 0.5 when symmetric]) × amplitude, honoring angle
// masking, painted exclusion weights, boundary falloff, and overhang
// protection. All copies of a welded position move identically → watertight.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "core/geometry.h"
#include "core/mapping.h"

namespace core {

// Raw RGBA8 pixels (Canvas ImageData layout, row 0 = image top).
struct ImageDataRGBA {
  std::vector<uint8_t> data; // w*h*4
  int width = 0;
  int height = 0;
};

struct DisplacementSettings {
  int mappingMode = MODE_TRIPLANAR;
  double scaleU = 1, scaleV = 1;
  double offsetU = 0, offsetV = 0;
  double rotation = 0;
  double amplitude = 0;
  bool symmetricDisplacement = false;
  bool noDownwardZ = false;
  double bottomAngleLimit = 0;
  double topAngleLimit = 0;
  double mappingBlend = 0;
  double seamBandWidth = 0.5;
  double capAngle = 20;
  std::optional<double> cylinderCenterX;
  std::optional<double> cylinderCenterY;
  std::optional<double> cylinderRadius;
  double boundaryFalloff = 0;
  double blendNormalSmoothing = 0;

  MappingSettings toMapping(double aspectU, double aspectV) const {
    MappingSettings m;
    m.scaleU = scaleU;
    m.scaleV = scaleV;
    m.offsetU = offsetU;
    m.offsetV = offsetV;
    m.rotation = rotation;
    m.textureAspectU = aspectU;
    m.textureAspectV = aspectV;
    m.mappingBlend = mappingBlend;
    m.seamBandWidth = seamBandWidth;
    m.capAngle = capAngle;
    m.cylinderCenterX = cylinderCenterX;
    m.cylinderCenterY = cylinderCenterY;
    m.cylinderRadius = cylinderRadius;
    return m;
  }
};

// Bilinear greyscale sample with wrapping texel neighbourhood (GL repeat
// semantics, V flipped like three.js flipY textures). Exposed for tests.
double sampleBilinear(const uint8_t* data, int w, int h, double u, double v);

// `excludeWeight`: optional per-vertex weights (size = vertexCount) threaded
// through by subdivision — empty means none.
Geometry applyDisplacement(const Geometry& geometry, const ImageDataRGBA& image,
                           const DisplacementSettings& settings,
                           const Bounds& bounds,
                           const std::vector<float>& excludeWeight = {},
                           const std::function<void(double)>& onProgress = nullptr);

} // namespace core
