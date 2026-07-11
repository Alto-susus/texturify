#include "core/smart_resolution.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "core/jsmath.h"
#include "core/loaders.h"
#include "core/mapping.h"
#include "core/texture_analysis.h"

namespace core {

namespace {

constexpr double PI = 3.14159265358979323846;
// Conservative BASE of the subdivision safety cap — Smart budgets against 16M
// on every machine so its suggestion is device-independent.
constexpr double HARD_CAP_TRIANGLES = 16'000'000;
constexpr double HARD_CAP_HEADROOM = 0.75;
// Equilateral-cover constant: triangles per (edge² × area).
const double TRIS_PER_AREA_GEOM = 4 / std::sqrt(3.0); // ≈ 2.309

constexpr double DECIM_COARSEN = 1.0;
constexpr double DECIM_REF_AMP = 0.5;
constexpr double DECIM_MIN_AMP = 0.1;
constexpr int64_t DECIM_MIN_TRI = 10'000;
constexpr int64_t DECIM_MAX_TRI = 2'000'000;

struct WorldPeriod {
  double periodU_mm, periodV_mm;
};

// World-space texture period along U/V — mirrors mapping.js computeUV math.
// NOTE: main.js passes the persistent settings object, which never carries
// textureAspectU/V (those only exist on the per-call copies made inside
// updatePreview/applyDisplacement) — so `settings.textureAspect? ?? 1` is
// always 1 here.
WorldPeriod computeWorldPeriod(const DisplacementSettings& settings,
                               const Bounds& bounds) {
  const double aspectU = 1, aspectV = 1;
  const Vec3& size = bounds.size;
  double sU = (settings.scaleU != 0 ? settings.scaleU : 1e-6) / aspectU;
  double sV = (settings.scaleV != 0 ? settings.scaleV : 1e-6) / aspectV;

  double md = std::max({size.x, size.y, size.z, 1e-6});
  double planar = md * sU;
  double planarV = md * sV;

  switch (settings.mappingMode) {
    case MODE_PLANAR_XY:
    case MODE_PLANAR_XZ:
    case MODE_PLANAR_YZ:
      return {planar, planarV};

    case MODE_CYLINDRICAL: {
      double rDefault = std::max(size.x, size.y) * 0.5;
      double r = std::max(settings.cylinderRadius.value_or(rDefault), 1e-6);
      double C = 2 * PI * r;
      return {C * sU, C * sV};
    }

    case MODE_SPHERICAL: {
      double r = std::max(0.5 * std::max({size.x, size.y, size.z}), 1e-6);
      return {2 * PI * r * sU, PI * r * sV};
    }

    case MODE_TRIPLANAR:
    case MODE_CUBIC:
    default:
      return {planar, planarV};
  }
}

// ── Subdivision triangle-count simulator ────────────────────────────────────
double simTri(double a, double b, double c, double T,
              std::unordered_map<double, double>& memo, int depth) {
  // Sort descending: a ≥ b ≥ c.
  if (a < b) std::swap(a, b);
  if (b < c) std::swap(b, c);
  if (a < b) std::swap(a, b);

  // Quantise relative to T for the cache (256 bins per multiple of T).
  double ka = js::round((a / T) * 256);
  double kb = js::round((b / T) * 256);
  double kc = js::round((c / T) * 256);
  double key = ka * 0x40000000 + kb * 0x10000 + kc;
  auto it = memo.find(key);
  if (it != memo.end()) return it->second;

  // Match subdivide()'s 12-pass outer cap.
  if (depth > 12) {
    memo[key] = 1;
    return 1;
  }

  bool sa = a > T, sb = b > T, sc = c > T;
  int n = (sa ? 1 : 0) + (sb ? 1 : 0) + (sc ? 1 : 0);
  if (n == 0) {
    memo[key] = 1;
    return 1;
  }

  double total;
  if (n == 3) {
    // 1→4 midpoint split.
    total = 4 * simTri(a / 2, b / 2, c / 2, T, memo, depth + 1);
  } else if (n == 1) {
    // 1→2 bisect (median m = ½√(2b² + 2c² − a²)).
    double m = 0.5 * std::sqrt(std::max(0.0, 2 * b * b + 2 * c * c - a * a));
    total = simTri(a / 2, b, m, T, memo, depth + 1) +
            simTri(a / 2, c, m, T, memo, depth + 1);
  } else {
    // n == 2: 1→3 fan.
    double m = 0.5 * std::sqrt(std::max(0.0, 2 * b * b + 2 * c * c - a * a));
    total = simTri(c, a / 2, m, T, memo, depth + 1) +
            simTri(m, c / 2, b / 2, T, memo, depth + 1) +
            simTri(b / 2, c / 2, a / 2, T, memo, depth + 1);
  }

  memo[key] = total;
  return total;
}

std::vector<double> computeTriEdges(const Geometry& geometry) {
  const auto& pos = geometry.positions;
  size_t triCount = pos.size() / 9;
  std::vector<double> out(triCount * 3);
  for (size_t t = 0; t < triCount; t++) {
    size_t o = t * 9;
    double ax = pos[o], ay = pos[o + 1], az = pos[o + 2];
    double bx = pos[o + 3], by = pos[o + 4], bz = pos[o + 5];
    double cx = pos[o + 6], cy = pos[o + 7], cz = pos[o + 8];
    out[t * 3] = std::hypot(bx - ax, by - ay, bz - az);
    out[t * 3 + 1] = std::hypot(cx - bx, cy - by, cz - bz);
    out[t * 3 + 2] = std::hypot(ax - cx, ay - cy, az - cz);
  }
  return out;
}

double simulateFromEdges(const std::vector<double>& triEdges, double edge) {
  std::unordered_map<double, double> memo;
  size_t triCount = triEdges.size() / 3;
  double total = 0;
  for (size_t i = 0; i < triCount; i++) {
    size_t o = i * 3;
    double a = triEdges[o], b = triEdges[o + 1], c = triEdges[o + 2];
    if (a <= edge && b <= edge && c <= edge) {
      total += 1;
      continue;
    }
    total += simTri(a, b, c, edge, memo, 0);
  }
  return total;
}

} // namespace

int64_t computeRecommendedMaxTri(double pixelsPerEdge, double pixMm,
                                 double surfaceArea, double amplitude) {
  if (!(pixelsPerEdge > 0) || !(pixMm > 0) || !(surfaceArea > 0))
    return DECIM_MIN_TRI;
  double absAmp = std::abs(amplitude);
  double ampScale = std::sqrt(DECIM_REF_AMP / std::max(absAmp, DECIM_MIN_AMP));
  double targetEdge = DECIM_COARSEN * pixelsPerEdge * pixMm * ampScale;
  double raw = TRIS_PER_AREA_GEOM * surfaceArea / (targetEdge * targetEdge);
  int64_t stepped = (int64_t)js::round(raw / 10'000) * 10'000;
  return std::max(DECIM_MIN_TRI, std::min(DECIM_MAX_TRI, stepped));
}

double estimateSubdivisionTriCount(const Geometry& geometry, double edge) {
  if (geometry.positions.empty()) return 0;
  return simulateFromEdges(computeTriEdges(geometry), edge);
}

SmartResolutionResult computeSmartResolution(const Geometry& geometry,
                                             const Bounds& bounds,
                                             const DisplacementSettings& settings,
                                             const ImageDataRGBA& texture) {
  SmartResolutionResult result;
  if (geometry.positions.empty() || texture.data.empty()) return result;

  // 1. Texture detail → pixels-per-edge.
  TextureAnalysis ta = analyzeTexture(texture);

  // 2. World-space pixel size (aspect = 1, matching the main.js call site).
  WorldPeriod wp = computeWorldPeriod(settings, bounds);
  double period_mm = std::min(wp.periodU_mm, wp.periodV_mm);
  double texW = texture.width > 0 ? texture.width : 512;
  double texH = texture.height > 0 ? texture.height : 512;
  double pixUmm = wp.periodU_mm / texW;
  double pixVmm = wp.periodV_mm / texH;
  double pixMm = std::min(pixUmm, pixVmm);

  // 3. Detail-driven edge length (Nyquist-style).
  double detailEdge = pixMm * ta.pixelsPerEdge;

  // 4. Surface area & triangle budget.
  double surfaceArea = computeSurfaceArea(geometry);
  double triBudget = HARD_CAP_TRIANGLES * HARD_CAP_HEADROOM;

  std::vector<double> triEdges = computeTriEdges(geometry);

  // Solve for the coarsest edge that keeps simulated count ≤ budget.
  double budgetEdge =
      std::sqrt((TRIS_PER_AREA_GEOM * surfaceArea) / std::max(triBudget, 1.0));
  for (int step = 0; step < 3; step++) {
    double simCount = simulateFromEdges(triEdges, budgetEdge);
    if (simCount <= triBudget) break;
    double correction = std::sqrt(simCount / triBudget);
    if (correction < 1.005) break; // converged
    budgetEdge = budgetEdge * correction;
  }
  // Guarantee the budget holds (sim count is a step function on uniform
  // meshes; walk coarser in 5% steps).
  for (int step = 0; step < 24; step++) {
    if (simulateFromEdges(triEdges, budgetEdge) <= triBudget) break;
    budgetEdge *= 1.05;
  }

  // 5. Final edge: the larger (coarser) of detail vs budget.
  double edge = std::max(detailEdge, budgetEdge);
  bool budgetClamped = budgetEdge > detailEdge;

  // Sanity clamp: never below 0.05mm, never coarser than min(5, diag/50).
  double diag = std::sqrt(bounds.size.x * bounds.size.x +
                          bounds.size.y * bounds.size.y +
                          bounds.size.z * bounds.size.z);
  double lo = 0.05;
  double hi = std::min(5.0, diag / 50);
  double preClamp = edge;
  edge = std::min(std::max(edge, lo), std::max(hi, lo));
  bool edgeClamped = edge != preClamp;

  // Round UP to 2 decimals so the slider value never violates the budget.
  edge = std::max(lo, std::ceil(edge * 100) / 100);

  double estTriangles = simulateFromEdges(triEdges, edge);

  int64_t recommendedMaxTri = computeRecommendedMaxTri(
      ta.pixelsPerEdge, pixMm, surfaceArea, settings.amplitude);

  result.ok = true;
  result.edge = edge;
  result.diagnostics = {ta.pixelsPerEdge,
                        ta.meanGrad,
                        ta.sharpFrac,
                        pixMm,
                        period_mm,
                        surfaceArea,
                        detailEdge,
                        budgetEdge,
                        estTriangles,
                        triBudget,
                        budgetClamped,
                        edgeClamped,
                        recommendedMaxTri};
  return result;
}

} // namespace core
