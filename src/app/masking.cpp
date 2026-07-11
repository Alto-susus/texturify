#include "app/masking.h"

#include <cmath>
#include <unordered_map>

#include "core/mesh_index.h"

namespace app {

namespace {

// Per-face combined mask (angle + user exclusion), mirroring the vertex
// shader so the preview boundary matches export. Shared by the falloff and
// boundary-edge passes.
void combinedFaceMask(const core::Geometry& geo,
                      const std::vector<float>& faceNormalAttr,
                      const std::vector<float>& userMaskArr,
                      const MaskSettings& s, std::vector<float>& faceMask,
                      std::vector<uint8_t>& isUserMasked) {
  const size_t triCount = geo.positions.size() / 9;
  faceMask.assign(triCount, 0.0f);
  isUserMasked.assign(triCount, 0);
  const bool hasFn = faceNormalAttr.size() == geo.positions.size();
  for (size_t t = 0; t < triCount; t++) {
    const double userVal = userMaskArr[t * 3];
    if (userVal < 0.5) { faceMask[t] = 0; isUserMasked[t] = 1; continue; }
    double angleMask = 1.0;
    if (hasFn) {
      const double fnx = faceNormalAttr[t * 9];
      const double fny = faceNormalAttr[t * 9 + 1];
      const double fnz = faceNormalAttr[t * 9 + 2];
      const double len = std::sqrt(fnx * fnx + fny * fny + fnz * fnz);
      const double nz = len > 1e-6 ? fnz / len : 0;
      const double surfaceAngle =
          std::acos(std::min(1.0, std::abs(nz))) * (180.0 / 3.141592653589793);
      if (nz < 0 && s.bottomAngleLimit >= 1)
        angleMask = surfaceAngle > s.bottomAngleLimit ? 1.0 : 0.0;
      if (nz >= 0 && s.topAngleLimit >= 1)
        angleMask =
            std::min(angleMask, surfaceAngle > s.topAngleLimit ? 1.0 : 0.0);
    }
    faceMask[t] = (float)angleMask;
  }
}

// computeBoundaryFalloffAttr(): per-vertex falloff ramp 0 (at boundary) → 1
// (at/beyond the falloff distance) + nearest boundary's mask type.
void computeBoundaryFalloffAttr(const core::Geometry& geo,
                                const std::vector<float>& faceNormalAttr,
                                const std::vector<float>& userMaskArr,
                                const MaskSettings& s, MaskAttributes& out) {
  const std::vector<float>& pos = geo.positions;
  const size_t posCount = pos.size() / 3;
  const size_t triCount = posCount / 3;
  const double falloff = s.boundaryFalloff;

  out.boundaryFalloff.assign(posCount, 1.0f);
  out.boundaryMaskType.assign(posCount, 1.0f);
  if (falloff <= 0) return;

  std::vector<float> faceMask;
  std::vector<uint8_t> isUserMasked;
  combinedFaceMask(geo, faceNormalAttr, userMaskArr, s, faceMask, isUserMasked);

  // Weld vertices to unique-position ids; accumulate per-id areas.
  core::QuantizedPointMap weldMap(1e4, std::min(posCount, (size_t)1 << 22));
  size_t nUnique = 0;
  std::vector<uint32_t> vertId(posCount);
  std::vector<double> idPosX(posCount), idPosY(posCount), idPosZ(posCount);
  std::vector<double> maskedArea(posCount, 0), totalArea(posCount, 0),
      userMaskArea(posCount, 0);

  for (size_t t = 0; t < triCount; t++) {
    const size_t b = t * 9;
    const double e1x = pos[b + 3] - pos[b], e1y = pos[b + 4] - pos[b + 1],
                 e1z = pos[b + 5] - pos[b + 2];
    const double e2x = pos[b + 6] - pos[b], e2y = pos[b + 7] - pos[b + 1],
                 e2z = pos[b + 8] - pos[b + 2];
    const double fx = e1y * e2z - e1z * e2y;
    const double fy = e1z * e2x - e1x * e2z;
    const double fz = e1x * e2y - e1y * e2x;
    const double area = std::sqrt(fx * fx + fy * fy + fz * fz);
    const bool masked = faceMask[t] < 0.5f;

    for (int v = 0; v < 3; v++) {
      const double px = pos[b + v * 3], py = pos[b + v * 3 + 1],
                   pz = pos[b + v * 3 + 2];
      const int32_t id = weldMap.getOrSet(px, py, pz, (int32_t)nUnique);
      if (weldMap.inserted) {
        nUnique++;
        idPosX[id] = px; idPosY[id] = py; idPosZ[id] = pz;
      }
      vertId[t * 3 + v] = (uint32_t)id;
      if (masked) maskedArea[id] += area;
      totalArea[id] += area;
      if (isUserMasked[t]) userMaskArea[id] += area;
    }
  }

  // Boundary positions: shared between masked and non-masked faces.
  struct BoundaryPos {
    double x, y, z;
    int maskType; // 0 = user, 1 = angle
  };
  std::vector<BoundaryPos> boundaryPositions;
  for (size_t id = 0; id < nUnique; id++) {
    const double frac = totalArea[id] > 0 ? maskedArea[id] / totalArea[id] : 0;
    if (frac > 0 && frac < 1)
      boundaryPositions.push_back({idPosX[id], idPosY[id], idPosZ[id],
                                   userMaskArea[id] > 0 ? 0 : 1});
  }
  if (boundaryPositions.empty()) return;

  // Spatial grid of boundary positions for nearest-neighbor search
  double gMinX = 1e300, gMinY = 1e300, gMinZ = 1e300;
  double gMaxX = -1e300, gMaxY = -1e300, gMaxZ = -1e300;
  for (const auto& bp : boundaryPositions) {
    gMinX = std::min(gMinX, bp.x); gMaxX = std::max(gMaxX, bp.x);
    gMinY = std::min(gMinY, bp.y); gMaxY = std::max(gMaxY, bp.y);
    gMinZ = std::min(gMinZ, bp.z); gMaxZ = std::max(gMaxZ, bp.z);
  }
  const double gPad = falloff + 1e-3;
  gMinX -= gPad; gMinY -= gPad; gMinZ -= gPad;
  gMaxX += gPad; gMaxY += gPad; gMaxZ += gPad;

  const int gRes = std::max(
      4, std::min(128, (int)std::ceil(std::cbrt((double)boundaryPositions.size()) * 2)));
  double gDx = (gMaxX - gMinX) / gRes; if (gDx == 0) gDx = 1;
  double gDy = (gMaxY - gMinY) / gRes; if (gDy == 0) gDy = 1;
  double gDz = (gMaxZ - gMinZ) / gRes; if (gDz == 0) gDz = 1;
  std::unordered_map<int64_t, std::vector<int>> bGrid;
  auto bCellKey = [gRes](int ix, int iy, int iz) -> int64_t {
    return ((int64_t)ix * gRes + iy) * gRes + iz;
  };
  auto cellIdx = [](double v, double gmin, double gd, int res) {
    return std::max(0, std::min(res - 1, (int)std::floor((v - gmin) / gd)));
  };
  for (int i = 0; i < (int)boundaryPositions.size(); i++) {
    const auto& bp = boundaryPositions[i];
    bGrid[bCellKey(cellIdx(bp.x, gMinX, gDx, gRes),
                   cellIdx(bp.y, gMinY, gDy, gRes),
                   cellIdx(bp.z, gMinZ, gDz, gRes))]
        .push_back(i);
  }

  const int searchX = (int)std::ceil(falloff / gDx);
  const int searchY = (int)std::ceil(falloff / gDy);
  const int searchZ = (int)std::ceil(falloff / gDz);

  // Per-unique-position falloff factor and mask type (-1 = keep default 1.0).
  std::vector<float> falloffById(nUnique, -1.0f), maskTypeById(nUnique, -1.0f);
  for (size_t id = 0; id < nUnique; id++) {
    const double frac = totalArea[id] > 0 ? maskedArea[id] / totalArea[id] : 0;
    if (frac >= 1) continue; // fully masked — mask zeroes it anyway
    if (frac > 0) {          // boundary vertex — distance 0
      falloffById[id] = 0;
      maskTypeById[id] = userMaskArea[id] > 0 ? 0.0f : 1.0f;
      continue;
    }
    const double px = idPosX[id], py = idPosY[id], pz = idPosZ[id];
    const int cix = cellIdx(px, gMinX, gDx, gRes);
    const int ciy = cellIdx(py, gMinY, gDy, gRes);
    const int ciz = cellIdx(pz, gMinZ, gDz, gRes);

    double minDist2 = falloff * falloff;
    int nearestType = 1;
    for (int dix = -searchX; dix <= searchX; dix++) {
      const int nix = cix + dix;
      if (nix < 0 || nix >= gRes) continue;
      for (int diy = -searchY; diy <= searchY; diy++) {
        const int niy = ciy + diy;
        if (niy < 0 || niy >= gRes) continue;
        for (int diz = -searchZ; diz <= searchZ; diz++) {
          const int niz = ciz + diz;
          if (niz < 0 || niz >= gRes) continue;
          auto it = bGrid.find(bCellKey(nix, niy, niz));
          if (it == bGrid.end()) continue;
          for (int bi : it->second) {
            const auto& bp = boundaryPositions[bi];
            const double dx = px - bp.x, dy = py - bp.y, dz = pz - bp.z;
            const double d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < minDist2) { minDist2 = d2; nearestType = bp.maskType; }
          }
        }
      }
    }
    const double factor = std::min(1.0, std::sqrt(minDist2) / falloff);
    if (factor < 1) {
      falloffById[id] = (float)factor;
      maskTypeById[id] = (float)nearestType;
    }
  }

  for (size_t i = 0; i < posCount; i++) {
    const uint32_t id = vertId[i];
    if (falloffById[id] >= 0) out.boundaryFalloff[i] = falloffById[id];
    if (maskTypeById[id] >= 0) out.boundaryMaskType[i] = maskTypeById[id];
  }
}

// computeBoundaryEdges(): boundary edge segments between masked and
// non-masked faces (JS Map insertion order, first MAX_EDGES=64).
void computeBoundaryEdges(const core::Geometry& geo,
                          const std::vector<float>& faceNormalAttr,
                          const std::vector<float>& userMaskArr,
                          const MaskSettings& s, MaskAttributes& out) {
  out.boundaryEdgeTexels.clear();
  out.boundaryEdgeCount = 0;
  const double falloff = s.boundaryFalloff;
  if (falloff <= 0) return;

  const std::vector<float>& pos = geo.positions;
  const size_t posCount = pos.size() / 3;
  const size_t triCount = posCount / 3;

  std::vector<float> faceMaskF;
  std::vector<uint8_t> isUserMasked;
  combinedFaceMask(geo, faceNormalAttr, userMaskArr, s, faceMaskF,
                   isUserMasked);
  std::vector<uint8_t> faceMaskBool(triCount);
  for (size_t t = 0; t < triCount; t++)
    faceMaskBool[t] = faceMaskF[t] > 0.5f ? 1 : 0;

  core::QuantizedPointMap weldMap(1e4, std::min(posCount, (size_t)1 << 22));
  size_t nUnique = 0;

  struct EdgeEntry {
    std::vector<int32_t> faces;
    double a[3], b[3];
  };
  // JS Map — preserve insertion order for the MAX_EDGES cutoff.
  std::unordered_map<int64_t, size_t> edgeIndex;
  std::vector<EdgeEntry> edgeList;
  const int64_t EKM = (int64_t)posCount;
  uint32_t ids[3];
  double ptx[3], pty[3], ptz[3];

  for (size_t t = 0; t < triCount; t++) {
    for (int v = 0; v < 3; v++) {
      const size_t vi = t * 3 + v;
      const double x = pos[vi * 3], y = pos[vi * 3 + 1], z = pos[vi * 3 + 2];
      const int32_t id = weldMap.getOrSet(x, y, z, (int32_t)nUnique);
      if (weldMap.inserted) nUnique++;
      ids[v] = (uint32_t)id; ptx[v] = x; pty[v] = y; ptz[v] = z;
    }
    for (int e = 0; e < 3; e++) {
      const int e2 = (e + 1) % 3;
      const int64_t a = ids[e], b = ids[e2];
      const int64_t edgeKey = a < b ? a * EKM + b : b * EKM + a;
      auto it = edgeIndex.find(edgeKey);
      if (it != edgeIndex.end()) {
        edgeList[it->second].faces.push_back((int32_t)t);
      } else {
        edgeIndex.emplace(edgeKey, edgeList.size());
        EdgeEntry entry;
        entry.faces.push_back((int32_t)t);
        entry.a[0] = ptx[e]; entry.a[1] = pty[e]; entry.a[2] = ptz[e];
        entry.b[0] = ptx[e2]; entry.b[1] = pty[e2]; entry.b[2] = ptz[e2];
        edgeList.push_back(std::move(entry));
      }
    }
  }

  constexpr int kMaxEdges = 64;
  std::vector<const EdgeEntry*> edges;
  for (const EdgeEntry& entry : edgeList) {
    if ((int)edges.size() >= kMaxEdges) break;
    bool hasMasked = false, hasTextured = false;
    for (int32_t f : entry.faces) {
      if (faceMaskBool[f] == 0) hasMasked = true;
      else hasTextured = true;
      if (hasMasked && hasTextured) break;
    }
    if (hasMasked && hasTextured) edges.push_back(&entry);
  }
  if (edges.empty()) return;

  out.boundaryEdgeTexels.resize(edges.size() * 8);
  for (size_t i = 0; i < edges.size(); i++) {
    const EdgeEntry& e = *edges[i];
    float* d = out.boundaryEdgeTexels.data() + i * 8;
    d[0] = (float)e.a[0]; d[1] = (float)e.a[1]; d[2] = (float)e.a[2]; d[3] = 0;
    d[4] = (float)e.b[0]; d[5] = (float)e.b[1]; d[6] = (float)e.b[2]; d[7] = 0;
  }
  out.boundaryEdgeCount = (int)edges.size();
}

} // namespace

MaskAttributes computeMaskAttributes(
    const core::Geometry& geo, const std::vector<float>& faceNormalAttr,
    const std::unordered_set<int32_t>& paintedFaces, bool selectionMode,
    const MaskSettings& settings, bool computeFalloff) {
  const size_t posCount = geo.positions.size() / 3;
  const size_t triCount = posCount / 3;
  MaskAttributes out;
  out.faceMask.assign(posCount, 1.0f);

  // updateFaceMask(): fast path when no user exclusion is active
  if (!paintedFaces.empty() || selectionMode) {
    for (size_t t = 0; t < triCount; t++) {
      const bool painted = paintedFaces.count((int32_t)t) > 0;
      const bool excluded = selectionMode ? !painted : painted;
      const float val = excluded ? 0.0f : 1.0f;
      out.faceMask[t * 3] = val;
      out.faceMask[t * 3 + 1] = val;
      out.faceMask[t * 3 + 2] = val;
    }
  }

  if (computeFalloff) {
    computeBoundaryFalloffAttr(geo, faceNormalAttr, out.faceMask, settings,
                               out);
    computeBoundaryEdges(geo, faceNormalAttr, out.faceMask, settings, out);
  } else {
    out.boundaryFalloff.assign(posCount, 1.0f);
    out.boundaryMaskType.assign(posCount, 1.0f);
  }
  return out;
}

double distSqPointToTri(double px, double py, double pz, double ax, double ay,
                        double az, double bx, double by, double bz, double cx,
                        double cy, double cz) {
  const double abx = bx - ax, aby = by - ay, abz = bz - az;
  const double acx = cx - ax, acy = cy - ay, acz = cz - az;
  const double apx = px - ax, apy = py - ay, apz = pz - az;

  const double d1 = abx * apx + aby * apy + abz * apz;
  const double d2 = acx * apx + acy * apy + acz * apz;
  if (d1 <= 0 && d2 <= 0) return apx * apx + apy * apy + apz * apz; // vertex A

  const double bpx = px - bx, bpy = py - by, bpz = pz - bz;
  const double d3 = abx * bpx + aby * bpy + abz * bpz;
  const double d4 = acx * bpx + acy * bpy + acz * bpz;
  if (d3 >= 0 && d4 <= d3) return bpx * bpx + bpy * bpy + bpz * bpz; // vertex B

  const double cpx = px - cx, cpy = py - cy, cpz = pz - cz;
  const double d5 = abx * cpx + aby * cpy + abz * cpz;
  const double d6 = acx * cpx + acy * cpy + acz * cpz;
  if (d6 >= 0 && d5 <= d6) return cpx * cpx + cpy * cpy + cpz * cpz; // vertex C

  const double vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) { // edge AB
    const double v = d1 / (d1 - d3);
    const double qx = ax + v * abx - px, qy = ay + v * aby - py,
                 qz = az + v * abz - pz;
    return qx * qx + qy * qy + qz * qz;
  }

  const double vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) { // edge AC
    const double w = d2 / (d2 - d6);
    const double qx = ax + w * acx - px, qy = ay + w * acy - py,
                 qz = az + w * acz - pz;
    return qx * qx + qy * qy + qz * qz;
  }

  const double va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) { // edge BC
    const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    const double qx = bx + w * (cx - bx) - px, qy = by + w * (cy - by) - py,
                 qz = bz + w * (cz - bz) - pz;
    return qx * qx + qy * qy + qz * qz;
  }

  // Inside triangle
  const double den = 1 / (va + vb + vc);
  const double v = vb * den, w = vc * den;
  const double qx = ax + abx * v + acx * w - px,
               qy = ay + aby * v + acy * w - py,
               qz = az + abz * v + acz * w - pz;
  return qx * qx + qy * qy + qz * qz;
}

void bfsBrushSelect(int32_t seedTriIdx, const core::Vec3& hitPt, double r2,
                    const core::Vec3& viewDir, const core::Geometry& geo,
                    const core::AdjacencyData& adj,
                    const std::function<void(int32_t)>& cb) {
  const size_t triCount = adj.adjacency.size();
  if (seedTriIdx < 0 || (size_t)seedTriIdx >= triCount) return;
  const std::vector<float>& pos = geo.positions;
  const std::vector<float>& faceNormals = adj.faceNormals;

  const double vdx = viewDir.x, vdy = viewDir.y, vdz = viewDir.z;
  const double hx = hitPt.x, hy = hitPt.y, hz = hitPt.z;

  std::vector<uint8_t> visited(triCount, 0);
  visited[seedTriIdx] = 1;
  std::vector<int32_t> queue{seedTriIdx};
  size_t head = 0;

  while (head < queue.size()) {
    const int32_t cur = queue[head++];
    const size_t b = (size_t)cur * 9;

    // Project each vertex onto the plane through hitPt perpendicular to
    // viewDir, then 3D point-to-triangle distance to the projected triangle.
    const double ax = pos[b], ay = pos[b + 1], az = pos[b + 2];
    const double bx = pos[b + 3], by = pos[b + 4], bz = pos[b + 5];
    const double cx = pos[b + 6], cy = pos[b + 7], cz = pos[b + 8];

    const double da = (ax - hx) * vdx + (ay - hy) * vdy + (az - hz) * vdz;
    const double db = (bx - hx) * vdx + (by - hy) * vdy + (bz - hz) * vdz;
    const double dc = (cx - hx) * vdx + (cy - hy) * vdy + (cz - hz) * vdz;

    const double d2 = distSqPointToTri(
        hx, hy, hz, ax - da * vdx, ay - da * vdy, az - da * vdz,
        bx - db * vdx, by - db * vdy, bz - db * vdz, cx - dc * vdx,
        cy - dc * vdy, cz - dc * vdz);
    if (d2 > r2) continue; // outside cylinder — don't paint, don't expand

    cb(cur);

    for (const core::AdjEntry& nb : adj.adjacency[cur]) {
      if (visited[nb.neighbor]) continue;
      visited[nb.neighbor] = 1;
      // Cull back-facing neighbors (dot ≥ 0 also culls the silhouette seam)
      const size_t nbi = (size_t)nb.neighbor * 3;
      const double dotN = faceNormals[nbi] * vdx + faceNormals[nbi + 1] * vdy +
                          faceNormals[nbi + 2] * vdz;
      if (dotN >= 0) continue;
      queue.push_back(nb.neighbor);
    }
  }
}

} // namespace app
