#include "core/mapping.h"

#include <algorithm>

namespace core {

namespace {

constexpr double TWO_PI = 2.0 * 3.14159265358979323846;
constexpr double PI = 3.14159265358979323846;
constexpr double CUBIC_AXIS_EPSILON = 1e-4;

UVSample applyTransform(double u, double v, double scaleU, double scaleV,
                        double offsetU, double offsetV, double cosR, double sinR) {
  double uu = u / scaleU + offsetU;
  double vv = v / scaleV + offsetV;
  if (cosR != 1 || sinR != 0) {
    uu -= 0.5;
    vv -= 0.5;
    double ru = cosR * uu - sinR * vv;
    double rv = sinR * uu + cosR * vv;
    uu = ru + 0.5;
    vv = rv + 0.5;
  }
  return {fract(uu), fract(vv), 1.0};
}

UVResult single(const UVSample& s) {
  UVResult r;
  r.triplanar = false;
  r.count = 1;
  r.samples[0] = s;
  r.samples[0].w = 1.0;
  return r;
}

} // namespace

CubicAxis getDominantCubicAxis(const Vec3& normal) {
  double ax = std::abs(normal.x);
  double ay = std::abs(normal.y);
  double az = std::abs(normal.z);
  // Near-ties count as a tie so 45° faces pick one stable axis instead of
  // flipping projection due to tiny normal jitter between triangles.
  if (ax >= ay - CUBIC_AXIS_EPSILON && ax >= az - CUBIC_AXIS_EPSILON)
    return CubicAxis::X;
  if (ay >= az - CUBIC_AXIS_EPSILON) return CubicAxis::Y;
  return CubicAxis::Z;
}

CubicWeights getCubicBlendWeights(const Vec3& normal, double blend,
                                  double seamBandWidth) {
  CubicAxis axis = getDominantCubicAxis(normal);
  double ax = std::abs(normal.x);
  double ay = std::abs(normal.y);
  double az = std::abs(normal.z);
  double primary = axis == CubicAxis::X ? ax : axis == CubicAxis::Y ? ay : az;
  double secondary = axis == CubicAxis::X   ? std::max(ay, az)
                     : axis == CubicAxis::Y ? std::max(ax, az)
                                            : std::max(ax, ay);

  CubicWeights oneHot{axis == CubicAxis::X ? 1.0 : 0.0,
                      axis == CubicAxis::Y ? 1.0 : 0.0,
                      axis == CubicAxis::Z ? 1.0 : 0.0};

  // blend=0: hard one-hot for sharp seams. The smooth branch handles an exact
  // 45° normal correctly (0.5/0.5), so no tie short-circuit when blend>0.
  if (blend <= 0.001) return oneHot;

  // Only blend inside a seam band around the cube-face boundary.
  double seamWidth = std::max(seamBandWidth, CUBIC_AXIS_EPSILON * 2);
  double seamMixRaw =
      1 - std::min(1.0, std::max(0.0, (primary - secondary) / seamWidth));
  double seamMix = blend * seamMixRaw * seamMixRaw * (3 - 2 * seamMixRaw);
  if (seamMix <= 0.001) return oneHot;

  // blend=1 → genuinely soft triplanar-style transition; lower values sharpen
  // progressively toward the dominant axis.
  double power = 1 + (1 - seamMix) * 11;
  double sx = std::pow(ax, power);
  double sy = std::pow(ay, power);
  double sz = std::pow(az, power);
  double smoothSum = sx + sy + sz + 1e-6;
  double smx = sx / smoothSum, smy = sy / smoothSum, smz = sz / smoothSum;

  double mx = oneHot.x * (1 - seamMix) + smx * seamMix;
  double my = oneHot.y * (1 - seamMix) + smy * seamMix;
  double mz = oneHot.z * (1 - seamMix) + smz * seamMix;
  double sum = mx + my + mz;

  return {mx / sum, my / sum, mz / sum};
}

UVResult computeUV(const Vec3& pos, const Vec3& normal, int mode,
                   const MappingSettings& settings, const Bounds& bounds) {
  const Vec3& min = bounds.min;
  const Vec3& size = bounds.size;
  const Vec3& center = bounds.center;
  // Compensate for non-square textures: divide scale by aspect correction so
  // equal world-space distances produce equal physical texture distances.
  double aU = settings.textureAspectU;
  double aV = settings.textureAspectV;
  double scaleU = settings.scaleU / aU;
  double scaleV = settings.scaleV / aV;
  double offsetU = settings.offsetU, offsetV = settings.offsetV;
  double rotRad = settings.rotation * PI / 180;
  double cosR = std::cos(rotRad);
  double sinR = std::sin(rotRad);
  double maxDim = std::max({size.x, size.y, size.z});
  double md = std::max(maxDim, 1e-6);

  double u = 0, v = 0;

  switch (mode) {
    case MODE_PLANAR_XY: {
      u = (pos.x - min.x) / md;
      v = (pos.y - min.y) / md;
      break;
    }
    case MODE_PLANAR_XZ: {
      u = (pos.x - min.x) / md;
      v = (pos.z - min.z) / md;
      break;
    }
    case MODE_PLANAR_YZ: {
      u = (pos.y - min.y) / md;
      v = (pos.z - min.z) / md;
      break;
    }

    case MODE_CYLINDRICAL: {
      // mappingBlend=0 → pure side projection (no cap seam);
      // mappingBlend>0 → smooth side↔cap blend. Cylinder axis is +Z.
      double cx = settings.cylinderCenterX.value_or(center.x);
      double cy = settings.cylinderCenterY.value_or(center.y);
      double r = std::max(
          settings.cylinderRadius.value_or(std::max(size.x, size.y) * 0.5), 1e-6);
      double C = TWO_PI * r;
      double rx = pos.x - cx;
      double ry = pos.y - cy;
      double blend = settings.mappingBlend;
      double theta = std::atan2(ry, rx);
      double uRaw = (theta / TWO_PI) + 0.5;
      double vSide = (pos.z - min.z) / C;

      // Seam smoothing: cross-fade between left/right texture continuations
      // at the atan2 wrap (both shifted by ±1.0 in raw space).
      double seamBand = settings.seamBandWidth * 0.1;
      double seamDist = std::min(uRaw, 1.0 - uRaw);
      bool inSeamZone = seamBand > 0.001 && seamDist < seamBand;

      UVSample sideSamples[2];
      int sideCount;
      if (inSeamZone) {
        double d = uRaw < 0.5 ? uRaw : uRaw - 1.0;
        double tRaw = (d + seamBand) / (2.0 * seamBand);
        double t = tRaw * tRaw * (3 - 2 * tRaw); // smoothstep
        UVSample tLeft = applyTransform(1.0 + d, vSide, scaleU, scaleV, offsetU,
                                        offsetV, cosR, sinR);
        UVSample tRight =
            applyTransform(d, vSide, scaleU, scaleV, offsetU, offsetV, cosR, sinR);
        sideSamples[0] = {tRight.u, tRight.v, t};
        sideSamples[1] = {tLeft.u, tLeft.v, 1 - t};
        sideCount = 2;
      } else {
        UVSample tSide = applyTransform(uRaw, vSide, scaleU, scaleV, offsetU,
                                        offsetV, cosR, sinR);
        sideSamples[0] = {tSide.u, tSide.v, 1};
        sideCount = 1;
      }

      auto sideResult = [&]() {
        if (sideCount == 1 && sideSamples[0].w == 1) return single(sideSamples[0]);
        UVResult res;
        res.triplanar = true;
        res.count = sideCount;
        res.samples[0] = sideSamples[0];
        res.samples[1] = sideSamples[1];
        return res;
      };

      if (blend <= 0.001) return sideResult();

      double capThreshold = std::cos(settings.capAngle * PI / 180);
      double blendHalf = settings.seamBandWidth * 0.5;
      double absnz = std::abs(normal.z);
      double capW = std::max(
          0.0, std::min(1.0, (absnz - (capThreshold - blendHalf)) /
                                 (2 * blendHalf + 1e-6)));

      if (capW <= 0) return sideResult();

      double uCap = rx / C + 0.5;
      double vCap = ry / C + 0.5;
      UVSample tCap =
          applyTransform(uCap, vCap, scaleU, scaleV, offsetU, offsetV, cosR, sinR);

      if (capW >= 1) return single(tCap);

      // Combine seam-blended side samples with the cap sample.
      UVResult res;
      res.triplanar = true;
      res.count = sideCount + 1;
      for (int i = 0; i < sideCount; i++) {
        res.samples[i] = {sideSamples[i].u, sideSamples[i].v,
                          sideSamples[i].w * (1 - capW)};
      }
      res.samples[sideCount] = {tCap.u, tCap.v, capW};
      return res;
    }

    case MODE_SPHERICAL: {
      double rx = pos.x - center.x;
      double ry = pos.y - center.y;
      double rz = pos.z - center.z;
      double r = std::sqrt(rx * rx + ry * ry + rz * rz);
      double phi = std::acos(
          std::max(-1.0, std::min(1.0, rz / std::max(r, 1e-6)))); // [0,PI], Z up
      double theta = std::atan2(ry, rx);                          // [-PI,PI]
      double uRaw = (theta / TWO_PI) + 0.5;
      double vRaw = phi / PI;

      // Seam smoothing: cross-fade at the atan2 wrap.
      double seamBand = settings.seamBandWidth * 0.1;
      double seamDist = std::min(uRaw, 1.0 - uRaw);
      if (seamBand > 0.001 && seamDist < seamBand) {
        double d = uRaw < 0.5 ? uRaw : uRaw - 1.0;
        double tRaw = (d + seamBand) / (2.0 * seamBand);
        double t = tRaw * tRaw * (3 - 2 * tRaw); // smoothstep
        UVSample tLeft = applyTransform(1.0 + d, vRaw, scaleU, scaleV, offsetU,
                                        offsetV, cosR, sinR);
        UVSample tRight =
            applyTransform(d, vRaw, scaleU, scaleV, offsetU, offsetV, cosR, sinR);
        UVResult res;
        res.triplanar = true;
        res.count = 2;
        res.samples[0] = {tRight.u, tRight.v, t};
        res.samples[1] = {tLeft.u, tLeft.v, 1 - t};
        return res;
      }

      u = uRaw;
      v = vRaw;
      break;
    }

    case MODE_CUBIC: {
      CubicWeights weights = getCubicBlendWeights(
          normal, settings.mappingBlend,
          settings.seamBandWidth); // app passes its seamBandWidth (default 0.35 arg
                                   // only when absent — settings always provide it)
      // Flip U based on normal sign so opposite faces show correct
      // (non-mirrored) text.
      double yzU = (pos.y - min.y) / md;
      if (normal.x < 0) yzU = -yzU;
      double xzU = (pos.x - min.x) / md;
      if (normal.y > 0) xzU = -xzU;
      double xyU = (pos.x - min.x) / md;
      if (normal.z < 0) xyU = -xyU;
      UVSample tYZ = applyTransform(yzU, (pos.z - min.z) / md, scaleU, scaleV,
                                    offsetU, offsetV, cosR, sinR);
      UVSample tXZ = applyTransform(xzU, (pos.z - min.z) / md, scaleU, scaleV,
                                    offsetU, offsetV, cosR, sinR);
      UVSample tXY = applyTransform(xyU, (pos.y - min.y) / md, scaleU, scaleV,
                                    offsetU, offsetV, cosR, sinR);

      if (weights.x > 0.999) return single(tYZ);
      if (weights.y > 0.999) return single(tXZ);
      if (weights.z > 0.999) return single(tXY);

      UVResult res;
      res.triplanar = true;
      res.count = 3;
      res.samples[0] = {tXY.u, tXY.v, weights.z};
      res.samples[1] = {tXZ.u, tXZ.v, weights.y};
      res.samples[2] = {tYZ.u, tYZ.v, weights.x};
      return res;
    }

    case MODE_TRIPLANAR:
    default: {
      // World-space normal blending
      double ax = std::abs(normal.x);
      double ay = std::abs(normal.y);
      double az = std::abs(normal.z);
      double ax2 = ax * ax, ay2 = ay * ay, az2 = az * az;
      double bx = ax2 * ax2;
      double by = ay2 * ay2;
      double bz = az2 * az2;
      double sum = bx + by + bz + 1e-6;
      double wx = bx / sum;
      double wy = by / sum;
      double wz = bz / sum;

      // Flip U based on normal sign so opposite faces show correct
      // (non-mirrored) text.
      double yzU = (pos.y - min.y) / md;
      if (normal.x < 0) yzU = -yzU;
      double xzU = (pos.x - min.x) / md;
      if (normal.y > 0) xzU = -xzU;
      double xyU = (pos.x - min.x) / md;
      if (normal.z < 0) xyU = -xyU;

      UVSample tXY = applyTransform(xyU, (pos.y - min.y) / md, scaleU, scaleV,
                                    offsetU, offsetV, cosR, sinR);
      UVSample tXZ = applyTransform(xzU, (pos.z - min.z) / md, scaleU, scaleV,
                                    offsetU, offsetV, cosR, sinR);
      UVSample tYZ = applyTransform(yzU, (pos.z - min.z) / md, scaleU, scaleV,
                                    offsetU, offsetV, cosR, sinR);

      UVResult res;
      res.triplanar = true;
      res.count = 3;
      res.samples[0] = {tXY.u, tXY.v, wz};
      res.samples[1] = {tXZ.u, tXZ.v, wy};
      res.samples[2] = {tYZ.u, tYZ.v, wx};
      return res;
    }
  }

  return single(applyTransform(u, v, scaleU, scaleV, offsetU, offsetV, cosR, sinR));
}

} // namespace core
