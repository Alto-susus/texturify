// Port of reference/js/mapping.js — CPU-side UV projection (7 modes), the
// exact JavaScript mirror of the GLSL in previewMaterial.js. Field defaults
// replicate the `??` fallbacks in the original.
#pragma once

#include <cmath>
#include <optional>

#include "core/geometry.h"

namespace core {

enum MappingMode : int {
  MODE_PLANAR_XY = 0,
  MODE_PLANAR_XZ = 1,
  MODE_PLANAR_YZ = 2,
  MODE_CYLINDRICAL = 3,
  MODE_SPHERICAL = 4,
  MODE_TRIPLANAR = 5,
  MODE_CUBIC = 6,
};

struct MappingSettings {
  double scaleU = 1, scaleV = 1;
  double offsetU = 0, offsetV = 0;
  double rotation = 0; // degrees
  double textureAspectU = 1, textureAspectV = 1;
  double mappingBlend = 0;
  double seamBandWidth = 0.5;
  double capAngle = 20;
  std::optional<double> cylinderCenterX; // default: bounds.center.x
  std::optional<double> cylinderCenterY; // default: bounds.center.y
  std::optional<double> cylinderRadius;  // default: max(size.x,size.y)*0.5
};

struct UVSample {
  double u = 0, v = 0, w = 1;
};

// Result of computeUV: single sample (triplanar=false, samples[0].{u,v}) or a
// weighted multi-sample blend (triplanar=true, up to 3 samples).
struct UVResult {
  bool triplanar = false;
  int count = 1;
  UVSample samples[3];
};

enum class CubicAxis { X, Y, Z };

struct CubicWeights {
  double x = 0, y = 0, z = 0;
};

CubicAxis getDominantCubicAxis(const Vec3& normal);
CubicWeights getCubicBlendWeights(const Vec3& normal, double blend,
                                  double seamBandWidth = 0.35);

UVResult computeUV(const Vec3& pos, const Vec3& normal, int mode,
                   const MappingSettings& settings, const Bounds& bounds);

// Fractional part, always positive (mirrors GLSL fract).
inline double fract(double x) { return x - std::floor(x); }

} // namespace core
