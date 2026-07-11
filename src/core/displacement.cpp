#include "core/displacement.h"

#include <algorithm>
#include <cmath>

#include "core/jsmath.h"
#include "core/mesh_index.h"

namespace core {

namespace {

constexpr double PI = 3.14159265358979323846;

// Apply scale/offset/rotation to raw UV for cubic projection (mirrors the
// private _cubicUV helper).
inline void cubicUV(double rawU, double rawV, const DisplacementSettings& s,
                    double rotRad, double aspectU, double aspectV, double& u,
                    double& v) {
  u = (rawU * aspectU) / s.scaleU + s.offsetU;
  v = (rawV * aspectV) / s.scaleV + s.offsetV;
  if (rotRad != 0) {
    double c = std::cos(rotRad), sn = std::sin(rotRad);
    u -= 0.5;
    v -= 0.5;
    double ru = c * u - sn * v, rv = sn * u + c * v;
    u = ru + 0.5;
    v = rv + 0.5;
  }
  u = u - std::floor(u);
  v = v - std::floor(v);
}

// JS % operator on doubles (fmod semantics: sign of dividend).
inline double jsmod(double a, double b) { return std::fmod(a, b); }

} // namespace

double sampleBilinear(const uint8_t* data, int w, int h, double u, double v) {
  // Ensure [0,1) — guard against floating-point edge cases
  u = jsmod(jsmod(u, 1.0) + 1.0, 1.0);
  v = jsmod(jsmod(v, 1.0) + 1.0, 1.0);
  // Flip V: ImageData row 0 is the image top, but v=0 is the texture bottom.
  v = 1 - v;

  double fx = u * w - 0.5;
  double fy = v * h - 0.5;
  double x0d = std::floor(fx);
  double y0d = std::floor(fy);
  double tx = fx - x0d;
  double ty = fy - y0d;
  // Wrapping neighbourhood (GL RepeatWrapping): (x0+1+w)%w with JS % on the
  // double, then int. Values are small so plain int math is exact.
  int64_t x0i = (int64_t)x0d;
  int64_t y0i = (int64_t)y0d;
  int64_t x1 = ((x0i + 1 + w) % w + w) % w;
  int64_t y1 = ((y0i + 1 + h) % h + h) % h;
  int64_t x0 = ((x0i % w) + w) % w;
  int64_t y0 = ((y0i % h) + h) % h;

  // Red channel — image is greyscale so R == G == B
  double v00 = data[(y0 * w + x0) * 4] / 255.0;
  double v10 = data[(y0 * w + x1) * 4] / 255.0;
  double v01 = data[(y1 * w + x0) * 4] / 255.0;
  double v11 = data[(y1 * w + x1) * 4] / 255.0;

  return v00 * (1 - tx) * (1 - ty) + v10 * tx * (1 - ty) + v01 * (1 - tx) * ty +
         v11 * tx * ty;
}

Geometry applyDisplacement(const Geometry& geometry, const ImageDataRGBA& image,
                           const DisplacementSettings& settings,
                           const Bounds& bounds,
                           const std::vector<float>& excludeWeight,
                           const std::function<void(double)>& onProgress) {
  const auto& pos = geometry.positions;
  const auto& nrm = geometry.normals;
  const size_t count = pos.size() / 3;
  const uint8_t* img = image.data.data();
  const int imgWidth = image.width, imgHeight = image.height;

  std::vector<float> newPos(count * 3);
  std::vector<float> newNrm(count * 3);

  // Texture aspect correction so non-square textures keep their proportions.
  double tmax = std::max({(double)imgWidth, (double)imgHeight, 1.0});
  double aspectU = tmax / std::max(imgWidth, 1);
  double aspectV = tmax / std::max(imgHeight, 1);
  MappingSettings mappingWithAspect = settings.toMapping(aspectU, aspectV);

  // 10 µm vertex-dedup cells — must match subdivision's QUANTISE grid.
  const double QUANT = 1e5;

  // ── Vertex dedup pass: position → numeric ID ──────────────────────────────
  const bool needIdPositions = settings.boundaryFalloff > 0;
  QuantizedPointMap dedupMap(QUANT, std::min(count, (size_t)1 << 22));
  uint32_t nextId = 0;
  std::vector<uint32_t> vertexId(count);
  std::vector<double> idPosX, idPosY, idPosZ;
  if (needIdPositions) {
    idPosX.resize(count);
    idPosY.resize(count);
    idPosZ.resize(count);
  }
  for (size_t i = 0; i < count; i++) {
    double x = pos[i * 3], y = pos[i * 3 + 1], z = pos[i * 3 + 2];
    int32_t id = dedupMap.getOrSet(x, y, z, (int32_t)nextId);
    if (dedupMap.inserted) {
      if (needIdPositions) {
        idPosX[id] = x;
        idPosY[id] = y;
        idPosZ[id] = z;
      }
      nextId++;
    }
    vertexId[i] = (uint32_t)id;
  }
  const size_t uniqueCount = nextId;

  // ── Pass 1: accumulate area-weighted smooth normals per unique position ──
  std::vector<double> smoothNrmX(uniqueCount, 0), smoothNrmY(uniqueCount, 0),
      smoothNrmZ(uniqueCount, 0);
  std::vector<double> zoneAreaX(uniqueCount, 0), zoneAreaY(uniqueCount, 0),
      zoneAreaZ(uniqueCount, 0);
  std::vector<double> maskedFracMasked(uniqueCount, 0), maskedFracTotal(uniqueCount, 0);

  const bool hasEw = !excludeWeight.empty();
  std::vector<uint8_t> userExcludedFaces(hasEw ? count / 3 : 0, 0);
  std::vector<uint8_t> excludedPos(hasEw ? uniqueCount : 0, 0);

  std::vector<double> dispCacheVal(uniqueCount, 0);
  std::vector<uint8_t> dispCacheSet(uniqueCount, 0);

  for (size_t t = 0; t < count; t += 3) {
    double vAx = pos[t * 3], vAy = pos[t * 3 + 1], vAz = pos[t * 3 + 2];
    double vBx = pos[(t + 1) * 3], vBy = pos[(t + 1) * 3 + 1], vBz = pos[(t + 1) * 3 + 2];
    double vCx = pos[(t + 2) * 3], vCy = pos[(t + 2) * 3 + 1], vCz = pos[(t + 2) * 3 + 2];
    double e1x = vBx - vAx, e1y = vBy - vAy, e1z = vBz - vAz;
    double e2x = vCx - vAx, e2y = vCy - vAy, e2z = vCz - vAz;
    // length = 2× triangle area → natural area weighting
    double fnx = e1y * e2z - e1z * e2y;
    double fny = e1z * e2x - e1x * e2z;
    double fnz = e1x * e2y - e1y * e2x;

    double faceArea = std::sqrt(fnx * fnx + fny * fny + fnz * fnz);
    double faceNzNorm = faceArea > 1e-12 ? fnz / faceArea : 0;
    double faceAngle = std::acos(std::abs(faceNzNorm)) * (180 / PI);
    bool angleMasked =
        faceNzNorm < 0
            ? (settings.bottomAngleLimit > 0 && faceAngle <= settings.bottomAngleLimit)
            : (settings.topAngleLimit > 0 && faceAngle <= settings.topAngleLimit);
    // Threshold >0.99 prevents shared-vertex MAX-propagation from marking
    // adjacent faces as excluded on closed meshes.
    bool userExcluded =
        hasEw && ((double)excludeWeight[t] + excludeWeight[t + 1] + excludeWeight[t + 2]) / 3 > 0.99;
    bool faceMasked = angleMasked;
    if (userExcluded) userExcludedFaces[t / 3] = 1;

    // Cubic: distribute this face's area across projection zones by blend
    // weights (one-hot when blend=0 → sharp seams preserved).
    double czX = 0, czY = 0, czZ = 0;
    if (settings.mappingMode == MODE_CUBIC && faceArea > 1e-12) {
      Vec3 unitFaceNrm{fnx / faceArea, fny / faceArea, fnz / faceArea};
      CubicWeights w =
          getCubicBlendWeights(unitFaceNrm, settings.mappingBlend, settings.seamBandWidth);
      czX = w.x * faceArea;
      czY = w.y * faceArea;
      czZ = w.z * faceArea;
    }

    for (int v = 0; v < 3; v++) {
      uint32_t vid = vertexId[t + v];
      if (userExcluded) excludedPos[vid] = 1;
      // Buffer normal (from subdivision) weighted by face area: smooth across
      // soft edges, sharp across hard (>30°) edges.
      double nx = nrm[(t + v) * 3], ny = nrm[(t + v) * 3 + 1], nz = nrm[(t + v) * 3 + 2];
      smoothNrmX[vid] += nx * faceArea;
      smoothNrmY[vid] += ny * faceArea;
      smoothNrmZ[vid] += nz * faceArea;
      if (czX > 1e-12 || czY > 1e-12 || czZ > 1e-12) {
        zoneAreaX[vid] += czX;
        zoneAreaY[vid] += czY;
        zoneAreaZ[vid] += czZ;
      }
      if (faceMasked) maskedFracMasked[vid] += faceArea;
      maskedFracTotal[vid] += faceArea;
    }
  }

  // Normalise; remember pre-normalisation magnitude / total area as a
  // reliability ratio (≈1 aligned, ≈0 knife-edge cancellation).
  std::vector<double> smoothNrmReliability(uniqueCount, 0);
  for (size_t id = 0; id < uniqueCount; id++) {
    double len = std::sqrt(smoothNrmX[id] * smoothNrmX[id] +
                           smoothNrmY[id] * smoothNrmY[id] +
                           smoothNrmZ[id] * smoothNrmZ[id]);
    double tA = maskedFracTotal[id];
    smoothNrmReliability[id] = (len > 0 && tA > 0) ? len / tA : 0;
    double inv = len > 0 ? 1 / len : 1;
    smoothNrmX[id] *= inv;
    smoothNrmY[id] *= inv;
    smoothNrmZ[id] *= inv;
  }

  // ── Pass 1.5: Laplacian-smoothed BLEND normal ────────────────────────────
  // Displacement direction stays on the unsmoothed smooth normal; only the
  // projection-blend weights read the smoothed one (kills seam noise from
  // per-vertex normal jitter inside blend bands).
  int blendNrmIters = std::max(0, (int)std::floor(settings.blendNormalSmoothing));
  const std::vector<double>*blendNrmX = &smoothNrmX, *blendNrmY = &smoothNrmY,
                           *blendNrmZ = &smoothNrmZ;
  std::vector<double> smX, smY, smZ;
  if (blendNrmIters > 0) {
    // Dedup-graph adjacency in CSR form (multigraph: repeated edges keep
    // their natural coupling weight).
    std::vector<uint32_t> degree(uniqueCount, 0);
    for (size_t t = 0; t < count; t += 3) {
      uint32_t a = vertexId[t], b = vertexId[t + 1], c = vertexId[t + 2];
      if (a != b) { degree[a]++; degree[b]++; }
      if (b != c) { degree[b]++; degree[c]++; }
      if (c != a) { degree[c]++; degree[a]++; }
    }
    std::vector<uint32_t> csrStart(uniqueCount + 1, 0);
    for (size_t id = 0; id < uniqueCount; id++)
      csrStart[id + 1] = csrStart[id] + degree[id];
    std::vector<uint32_t> neighbors(csrStart[uniqueCount]);
    std::vector<uint32_t> cursor(uniqueCount, 0);
    for (size_t t = 0; t < count; t += 3) {
      uint32_t a = vertexId[t], b = vertexId[t + 1], c = vertexId[t + 2];
      if (a != b) { neighbors[csrStart[a] + cursor[a]++] = b; neighbors[csrStart[b] + cursor[b]++] = a; }
      if (b != c) { neighbors[csrStart[b] + cursor[b]++] = c; neighbors[csrStart[c] + cursor[c]++] = b; }
      if (c != a) { neighbors[csrStart[c] + cursor[c]++] = a; neighbors[csrStart[a] + cursor[a]++] = c; }
    }

    std::vector<double> curX = smoothNrmX, curY = smoothNrmY, curZ = smoothNrmZ;
    std::vector<double> nxtX(uniqueCount), nxtY(uniqueCount), nxtZ(uniqueCount);

    for (int iter = 0; iter < blendNrmIters; iter++) {
      for (size_t id = 0; id < uniqueCount; id++) {
        uint32_t s = csrStart[id], e = csrStart[id + 1];
        if (e == s) {
          nxtX[id] = curX[id]; nxtY[id] = curY[id]; nxtZ[id] = curZ[id];
          continue;
        }
        double sx = 0, sy = 0, sz = 0;
        for (uint32_t k = s; k < e; k++) {
          uint32_t nb = neighbors[k];
          sx += curX[nb]; sy += curY[nb]; sz += curZ[nb];
        }
        double inv = 1.0 / (e - s);
        sx *= inv; sy *= inv; sz *= inv;
        double len = std::sqrt(sx * sx + sy * sy + sz * sz);
        if (len > 1e-12) {
          double r = 1 / len;
          nxtX[id] = sx * r; nxtY[id] = sy * r; nxtZ[id] = sz * r;
        } else {
          // Neighbour normals cancelled (knife-edge) — keep current.
          nxtX[id] = curX[id]; nxtY[id] = curY[id]; nxtZ[id] = curZ[id];
        }
      }
      std::swap(curX, nxtX);
      std::swap(curY, nxtY);
      std::swap(curZ, nxtZ);
    }
    smX = std::move(curX);
    smY = std::move(curY);
    smZ = std::move(curZ);
    blendNrmX = &smX;
    blendNrmY = &smY;
    blendNrmZ = &smZ;
  }

  // ── Boundary falloff distance field ──────────────────────────────────────
  double boundaryFalloff = settings.boundaryFalloff;
  std::vector<double> falloffArr;
  bool hasFalloff = false;

  if (boundaryFalloff > 0) {
    std::vector<double> bpX, bpY, bpZ;
    bpX.reserve(uniqueCount);
    bpY.reserve(uniqueCount);
    bpZ.reserve(uniqueCount);
    double gMinX = INFINITY, gMinY = INFINITY, gMinZ = INFINITY;
    double gMaxX = -INFINITY, gMaxY = -INFINITY, gMaxZ = -INFINITY;
    for (size_t id = 0; id < uniqueCount; id++) {
      double mfTotal = maskedFracTotal[id];
      double maskedFrac = mfTotal > 0 ? maskedFracMasked[id] / mfTotal : 0;
      bool isOnExclBoundary = hasEw && excludedPos[id] == 1;
      if (isOnExclBoundary || (maskedFrac > 0 && maskedFrac < 1)) {
        double x = idPosX[id], y = idPosY[id], z = idPosZ[id];
        bpX.push_back(x);
        bpY.push_back(y);
        bpZ.push_back(z);
        gMinX = std::min(gMinX, x); gMaxX = std::max(gMaxX, x);
        gMinY = std::min(gMinY, y); gMaxY = std::max(gMaxY, y);
        gMinZ = std::min(gMinZ, z); gMaxZ = std::max(gMaxZ, z);
      }
    }
    size_t bpCount = bpX.size();

    if (bpCount > 0) {
      double gPad = boundaryFalloff + 1e-3;
      gMinX -= gPad; gMinY -= gPad; gMinZ -= gPad;
      gMaxX += gPad; gMaxY += gPad; gMaxZ += gPad;

      int gRes = (int)std::max(4.0, std::min(128.0, std::ceil(std::cbrt((double)bpCount) * 2)));
      double gDx = (gMaxX - gMinX) / gRes; if (gDx == 0) gDx = 1;
      double gDy = (gMaxY - gMinY) / gRes; if (gDy == 0) gDy = 1;
      double gDz = (gMaxZ - gMinZ) / gRes; if (gDz == 0) gDz = 1;
      double invDx = 1 / gDx, invDy = 1 / gDy, invDz = 1 / gDz;
      size_t gridSize = (size_t)gRes * gRes * gRes;
      int gResMax = gRes - 1;

      // CSR-style spatial grid over boundary points.
      std::vector<uint32_t> cellCount(gridSize, 0);
      std::vector<uint32_t> bpCell(bpCount);
      auto cellOf = [&](double x, double y, double z) -> uint32_t {
        int ix = js::toInt32((x - gMinX) * invDx);
        if (ix < 0) ix = 0; else if (ix > gResMax) ix = gResMax;
        int iy = js::toInt32((y - gMinY) * invDy);
        if (iy < 0) iy = 0; else if (iy > gResMax) iy = gResMax;
        int iz = js::toInt32((z - gMinZ) * invDz);
        if (iz < 0) iz = 0; else if (iz > gResMax) iz = gResMax;
        return (uint32_t)((ix * gRes + iy) * gRes + iz);
      };
      for (size_t i = 0; i < bpCount; i++) {
        uint32_t ck = cellOf(bpX[i], bpY[i], bpZ[i]);
        bpCell[i] = ck;
        cellCount[ck]++;
      }
      std::vector<uint32_t> cellStart(gridSize + 1, 0);
      for (size_t c = 0; c < gridSize; c++) cellStart[c + 1] = cellStart[c] + cellCount[c];
      std::vector<uint32_t> cursor(gridSize, 0);
      std::vector<uint32_t> cellIdx(bpCount);
      for (size_t i = 0; i < bpCount; i++) {
        uint32_t ck = bpCell[i];
        cellIdx[cellStart[ck] + cursor[ck]++] = i;
      }

      int searchX = (int)std::ceil(boundaryFalloff * invDx);
      int searchY = (int)std::ceil(boundaryFalloff * invDy);
      int searchZ = (int)std::ceil(boundaryFalloff * invDz);
      double maxDist2 = boundaryFalloff * boundaryFalloff;
      double invFalloff = 1 / boundaryFalloff;

      falloffArr.assign(uniqueCount, 1.0); // default: full displacement
      hasFalloff = true;
      for (size_t id = 0; id < uniqueCount; id++) {
        double mfTotal = maskedFracTotal[id];
        double maskedFrac = mfTotal > 0 ? maskedFracMasked[id] / mfTotal : 0;
        bool isOnExclBoundary = hasEw && excludedPos[id] == 1;
        // Only compute falloff for fully-textured, non-boundary positions
        if (maskedFrac > 0 || isOnExclBoundary) continue;

        double px = idPosX[id], py = idPosY[id], pz = idPosZ[id];
        int cix = js::toInt32((px - gMinX) * invDx);
        if (cix < 0) cix = 0; else if (cix > gResMax) cix = gResMax;
        int ciy = js::toInt32((py - gMinY) * invDy);
        if (ciy < 0) ciy = 0; else if (ciy > gResMax) ciy = gResMax;
        int ciz = js::toInt32((pz - gMinZ) * invDz);
        if (ciz < 0) ciz = 0; else if (ciz > gResMax) ciz = gResMax;

        int nixLo = std::max(0, cix - searchX), nixHi = std::min(gResMax, cix + searchX);
        int niyLo = std::max(0, ciy - searchY), niyHi = std::min(gResMax, ciy + searchY);
        int nizLo = std::max(0, ciz - searchZ), nizHi = std::min(gResMax, ciz + searchZ);

        double minDist2 = maxDist2;
        for (int nix = nixLo; nix <= nixHi; nix++) {
          size_t baseX = (size_t)nix * gRes;
          for (int niy = niyLo; niy <= niyHi; niy++) {
            size_t baseXY = (baseX + niy) * gRes;
            for (int niz = nizLo; niz <= nizHi; niz++) {
              size_t ck = baseXY + niz;
              uint32_t end = cellStart[ck + 1];
              for (uint32_t k = cellStart[ck]; k < end; k++) {
                uint32_t idx = cellIdx[k];
                double dx = px - bpX[idx], dy = py - bpY[idx], dz = pz - bpZ[idx];
                double d2 = dx * dx + dy * dy + dz * dz;
                if (d2 < minDist2) minDist2 = d2;
              }
            }
          }
        }
        if (minDist2 < maxDist2) falloffArr[id] = std::sqrt(minDist2) * invFalloff;
      }
    }
  }

  // ── Pass 2: sample displacement texture once per unique position ─────────
  double rotRad = settings.rotation * PI / 180;
  for (size_t i = 0; i < count; i++) {
    uint32_t vid = vertexId[i];
    if (dispCacheSet[vid]) continue;
    dispCacheSet[vid] = 1;

    double posX = pos[i * 3], posY = pos[i * 3 + 1], posZ = pos[i * 3 + 2];

    // Cubic: blend weights from the smooth per-vertex normal (matches the
    // per-fragment preview); knife-edge fallback via reliability ratio.
    if (settings.mappingMode == MODE_CUBIC) {
      double md = std::max({bounds.size.x, bounds.size.y, bounds.size.z, 1e-6});

      double wX = 0, wY = 0, wZ = 0;
      if (smoothNrmReliability[vid] > 0.5) {
        Vec3 sn{(*blendNrmX)[vid], (*blendNrmY)[vid], (*blendNrmZ)[vid]};
        CubicWeights w =
            getCubicBlendWeights(sn, settings.mappingBlend, settings.seamBandWidth);
        wX = w.x; wY = w.y; wZ = w.z;
      } else {
        double zaX = zoneAreaX[vid], zaY = zoneAreaY[vid], zaZ = zoneAreaZ[vid];
        double total = zaX + zaY + zaZ;
        if (total > 0) { wX = zaX / total; wY = zaY / total; wZ = zaZ / total; }
      }

      if (wX + wY + wZ > 0) {
        double grey = 0;
        // U-flip uses the *original* smoothNrm — a discrete sign decision
        // about which cube face this vertex sits on.
        double u, v;
        if (wX > 0) { // X-dominant → YZ projection
          double rawU = (posY - bounds.min.y) / md;
          if (smoothNrmX[vid] < 0) rawU = -rawU;
          cubicUV(rawU, (posZ - bounds.min.z) / md, settings, rotRad, aspectU, aspectV, u, v);
          grey += sampleBilinear(img, imgWidth, imgHeight, u, v) * wX;
        }
        if (wY > 0) { // Y-dominant → XZ projection
          double rawU = (posX - bounds.min.x) / md;
          if (smoothNrmY[vid] > 0) rawU = -rawU;
          cubicUV(rawU, (posZ - bounds.min.z) / md, settings, rotRad, aspectU, aspectV, u, v);
          grey += sampleBilinear(img, imgWidth, imgHeight, u, v) * wY;
        }
        if (wZ > 0) { // Z-dominant → XY projection
          double rawU = (posX - bounds.min.x) / md;
          if (smoothNrmZ[vid] < 0) rawU = -rawU;
          cubicUV(rawU, (posY - bounds.min.y) / md, settings, rotRad, aspectU, aspectV, u, v);
          grey += sampleBilinear(img, imgWidth, imgHeight, u, v) * wZ;
        }
        dispCacheVal[vid] = grey;
        continue;
      }
    }

    // Triplanar / cylindrical seam blends use the SMOOTHED blend normal.
    Vec3 tmpPos{posX, posY, posZ};
    Vec3 tmpNrm{(*blendNrmX)[vid], (*blendNrmY)[vid], (*blendNrmZ)[vid]};

    UVResult uvResult =
        computeUV(tmpPos, tmpNrm, settings.mappingMode, mappingWithAspect, bounds);
    double grey;
    if (uvResult.triplanar) {
      grey = 0;
      for (int s = 0; s < uvResult.count; s++) {
        grey += sampleBilinear(img, imgWidth, imgHeight, uvResult.samples[s].u,
                               uvResult.samples[s].v) *
                uvResult.samples[s].w;
      }
    } else {
      grey = sampleBilinear(img, imgWidth, imgHeight, uvResult.samples[0].u,
                            uvResult.samples[0].v);
    }
    dispCacheVal[vid] = grey;
  }

  // ── Pass 3: displace every vertex copy by the same vector ────────────────
  const size_t REPORT_EVERY = 5000;

  for (size_t i = 0; i < count; i++) {
    double posX = pos[i * 3], posY = pos[i * 3 + 1], posZ = pos[i * 3 + 2];
    double nrmX = nrm[i * 3], nrmY = nrm[i * 3 + 1], nrmZ = nrm[i * 3 + 2];

    uint32_t vid = vertexId[i];
    double grey = dispCacheVal[vid];

    bool isFaceExcluded = hasEw && userExcludedFaces[i / 3];
    // Pin included-face vertices sharing a position with an excluded face —
    // seals the crack at the mask boundary (watertight, decimator-safe).
    bool isSealedBoundary = !isFaceExcluded && hasEw && excludedPos[vid] == 1;
    double mfTotal = maskedFracTotal[vid];
    double maskedFrac = mfTotal > 0 ? maskedFracMasked[vid] / mfTotal : 0;
    double centeredGrey = settings.symmetricDisplacement ? (grey - 0.5) : grey;
    double falloffFactor = hasFalloff ? falloffArr[vid] : 1.0;
    double disp = (isFaceExcluded || isSealedBoundary)
                      ? 0
                      : falloffFactor * (1 - maskedFrac) * centeredGrey * settings.amplitude;

    double newX = posX + smoothNrmX[vid] * disp;
    double newY = posY + smoothNrmY[vid] * disp;
    double newZ = posZ + smoothNrmZ[vid] * disp;

    // Prevent boundary vertices from poking through the masked surface in Z.
    if (maskedFrac > 0) {
      if (settings.bottomAngleLimit > 0 && newZ < posZ) newZ = posZ;
      if (settings.topAngleLimit > 0 && newZ > posZ) newZ = posZ;
    }

    // Overhang protection: never move a vertex below its original Z.
    if (settings.noDownwardZ && newZ < posZ) newZ = posZ;

    // Bottom-plane flat clamp: with overhang protection on, also clamp upward
    // motion of vertices that sat on the print bottom plane.
    if (settings.noDownwardZ && posZ <= bounds.min.z + 1e-5) newZ = posZ;

    newPos[i * 3] = (float)newX;
    newPos[i * 3 + 1] = (float)newY;
    newPos[i * 3 + 2] = (float)newZ;

    // Keep per-face normal for shading (recomputed below anyway)
    newNrm[i * 3] = (float)nrmX;
    newNrm[i * 3 + 1] = (float)nrmY;
    newNrm[i * 3 + 2] = (float)nrmZ;

    if (onProgress && i % REPORT_EVERY == 0) onProgress((double)i / count);
  }

  // Exact per-face normals from displaced positions (cross product per
  // triangle — unambiguous, matches winding order).
  for (size_t t = 0; t < count; t += 3) {
    double ax = newPos[t * 3], ay = newPos[t * 3 + 1], az = newPos[t * 3 + 2];
    double bx = newPos[t * 3 + 3], by = newPos[t * 3 + 4], bz = newPos[t * 3 + 5];
    double cx = newPos[t * 3 + 6], cy = newPos[t * 3 + 7], cz = newPos[t * 3 + 8];
    double eAx = bx - ax, eAy = by - ay, eAz = bz - az;
    double eBx = cx - ax, eBy = cy - ay, eBz = cz - az;
    double fnx = eAy * eBz - eAz * eBy;
    double fny = eAz * eBx - eAx * eBz;
    double fnz = eAx * eBy - eAy * eBx;
    double len = std::sqrt(fnx * fnx + fny * fny + fnz * fnz);
    // three.js Vector3.normalize: divide by length, or leave 0-vector as-is
    if (len > 0) { fnx /= len; fny /= len; fnz /= len; }
    for (int v = 0; v < 3; v++) {
      newNrm[(t + v) * 3] = (float)fnx;
      newNrm[(t + v) * 3 + 1] = (float)fny;
      newNrm[(t + v) * 3 + 2] = (float)fnz;
    }
  }

  Geometry out;
  out.positions = std::move(newPos);
  out.normals = std::move(newNrm);
  return out;
}

} // namespace core
