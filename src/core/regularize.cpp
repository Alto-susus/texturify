#include "core/regularize.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/loaders.h"
#include "core/mesh_index.h"

namespace core {

namespace {
constexpr double QUANTISE = 1e5;
constexpr double PI = 3.14159265358979323846;
} // namespace

RegularizeResult regularizeMesh(const Geometry& geometry,
                                const std::vector<int32_t>& faceParentId,
                                double maxEdgeLength, const RegularizeOpts& opts,
                                const std::vector<float>* excludeWeight) {
  const double aspectThreshold = opts.aspectThreshold;
  // Two-tier slack: loose base so non-sliver boundary collapses still succeed
  // (gives chains room to dissolve); aggressive tier when a wing is extreme.
  const double slack = opts.slack;
  const double aggressiveSlack = opts.aggressiveSlack;
  const double extremeSliverAspect = opts.extremeSliverAspect;
  const double maxNormalDeltaCos =
      opts.maxNormalDeltaCos >= -1 ? opts.maxNormalDeltaCos : std::cos(15 * PI / 180);
  const double aggressiveNormalDeltaCos =
      opts.aggressiveNormalDeltaCos >= -1 ? opts.aggressiveNormalDeltaCos
                                          : std::cos(25 * PI / 180);
  const double sharpEdgeCos =
      opts.sharpEdgeCos >= -1 ? opts.sharpEdgeCos : std::cos(30 * PI / 180);
  const int maxRounds = opts.maxRounds;

  const double baseMaxLenSqAllowed =
      (maxEdgeLength * slack) * (maxEdgeLength * slack);
  const double aggressiveMaxLenSqAllowed =
      (maxEdgeLength * aggressiveSlack) * (maxEdgeLength * aggressiveSlack);
  const double extremeAspect2 = extremeSliverAspect * extremeSliverAspect;

  // ── Build indexed mesh ──
  const auto& pa = geometry.positions;
  const size_t triCount = pa.size() / 9;

  QuantizedPointMap posMap(QUANTISE, std::min(triCount * 3, (size_t)1 << 22));
  std::vector<double> vertX, vertY, vertZ;
  vertX.reserve(triCount * 3);
  int32_t nextVid = 0;
  std::vector<int32_t> corners(triCount * 3);
  for (size_t i = 0; i < triCount * 3; i++) {
    double x = pa[i * 3], y = pa[i * 3 + 1], z = pa[i * 3 + 2];
    int32_t id = posMap.getOrSet(x, y, z, nextVid);
    if (posMap.inserted) {
      vertX.push_back(x);
      vertY.push_back(y);
      vertZ.push_back(z);
      nextVid++;
    }
    corners[i] = id;
  }

  // Per-triangle face normal (unit, float32 like the JS Float32Arrays) + flags
  std::vector<float> triNrmX(triCount), triNrmY(triCount), triNrmZ(triCount);
  std::vector<uint8_t> triDeleted(triCount, 0);
  std::vector<int32_t> newParentId = faceParentId;

  auto recomputeFaceNormal = [&](size_t t) {
    int32_t a = corners[t * 3], b = corners[t * 3 + 1], c = corners[t * 3 + 2];
    double ax = vertX[a], ay = vertY[a], az = vertZ[a];
    double e1x = vertX[b] - ax, e1y = vertY[b] - ay, e1z = vertZ[b] - az;
    double e2x = vertX[c] - ax, e2y = vertY[c] - ay, e2z = vertZ[c] - az;
    double nx = e1y * e2z - e1z * e2y;
    double ny = e1z * e2x - e1x * e2z;
    double nz = e1x * e2y - e1y * e2x;
    double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 0) {
      triNrmX[t] = (float)(nx / len);
      triNrmY[t] = (float)(ny / len);
      triNrmZ[t] = (float)(nz / len);
    } else {
      triNrmX[t] = 0;
      triNrmY[t] = 0;
      triNrmZ[t] = 0;
    }
  };
  for (size_t t = 0; t < triCount; t++) recomputeFaceNormal(t);

  // Original face normals — frozen at start; the normal-swing gate measures
  // against THESE so cumulative drift can't compound.
  std::vector<float> origNrmX = triNrmX, origNrmY = triNrmY, origNrmZ = triNrmZ;

  // vertex → incident triangles as intrusive doubly-linked corner-slot lists.
  std::vector<int32_t> vfHead((size_t)nextVid, -1);
  std::vector<int32_t> slotNext(triCount * 3), slotPrev(triCount * 3);
  auto linkSlot = [&](int32_t s) {
    int32_t v = corners[s];
    int32_t h = vfHead[v];
    slotPrev[s] = -1;
    slotNext[s] = h;
    if (h != -1) slotPrev[h] = s;
    vfHead[v] = s;
  };
  auto unlinkSlot = [&](int32_t s) {
    int32_t p = slotPrev[s], n = slotNext[s];
    if (p != -1) slotNext[p] = n;
    else vfHead[corners[s]] = n;
    if (n != -1) slotPrev[n] = p;
  };
  for (size_t s = 0; s < triCount * 3; s++) linkSlot((int32_t)s);

  // Stamp arrays for O(1) membership tests.
  std::vector<uint32_t> vertStamp((size_t)nextVid, 0);
  std::vector<uint32_t> triStamp(triCount, 0);
  uint32_t stampGen = 0;

  auto sqDist = [&](int32_t va, int32_t vb) {
    double dx = vertX[va] - vertX[vb];
    double dy = vertY[va] - vertY[vb];
    double dz = vertZ[va] - vertZ[vb];
    return dx * dx + dy * dy + dz * dz;
  };

  // Squared thinness = lmax⁴ / |AB × AC|² — catches near-collinear slivers
  // that the edge-ratio metric misses.
  auto triAspectSq = [&](size_t t) -> double {
    int32_t a = corners[t * 3], b = corners[t * 3 + 1], c = corners[t * 3 + 2];
    double ax = vertX[a], ay = vertY[a], az = vertZ[a];
    double abx = vertX[b] - ax, aby = vertY[b] - ay, abz = vertZ[b] - az;
    double acx = vertX[c] - ax, acy = vertY[c] - ay, acz = vertZ[c] - az;
    double bcx = vertX[c] - vertX[b], bcy = vertY[c] - vertY[b], bcz = vertZ[c] - vertZ[b];
    double lAB2 = abx * abx + aby * aby + abz * abz;
    double lAC2 = acx * acx + acy * acy + acz * acz;
    double lBC2 = bcx * bcx + bcy * bcy + bcz * bcz;
    double lmax2 = std::max({lAB2, lAC2, lBC2});
    double nx = aby * acz - abz * acy;
    double ny = abz * acx - abx * acz;
    double nz = abx * acy - aby * acx;
    double cross2 = nx * nx + ny * ny + nz * nz;
    return cross2 > 0 ? lmax2 * lmax2 / cross2
                      : std::numeric_limits<double>::infinity();
  };

  // Sharp-edge vertex freeze (sliver-aware: unreliable sliver dihedrals skip
  // the freeze; the normal-change gate remains the safeguard).
  std::vector<uint8_t> frozenVert((size_t)nextVid, 0);
  {
    std::vector<float> triThin2(triCount);
    for (size_t t = 0; t < triCount; t++) triThin2[t] = (float)triAspectSq(t);
    QuantizedPointMap edgeSeen(1, std::min(triCount * 3, (size_t)1 << 22));
    for (size_t t = 0; t < triCount; t++) {
      int32_t a = corners[t * 3], b = corners[t * 3 + 1], c = corners[t * 3 + 2];
      for (int e = 0; e < 3; e++) {
        int32_t u = e == 0 ? a : e == 1 ? b : c;
        int32_t v = e == 0 ? b : e == 1 ? c : a;
        int32_t lo = u < v ? u : v, hi = u < v ? v : u;
        int32_t other = edgeSeen.getOrSet(lo, hi, 0, (int32_t)t);
        if (edgeSeen.inserted) continue;
        if (triThin2[t] > extremeAspect2 || triThin2[other] > extremeAspect2) continue;
        double dot = (double)triNrmX[t] * triNrmX[other] +
                     (double)triNrmY[t] * triNrmY[other] +
                     (double)triNrmZ[t] * triNrmZ[other];
        if (dot < sharpEdgeCos) {
          frozenVert[u] = 1;
          frozenVert[v] = 1;
        }
      }
    }
  }

  // Triangles containing both u and v (reused scratch).
  std::vector<int32_t> wingScratch;
  auto trianglesSharingEdge = [&](int32_t u, int32_t v) -> std::vector<int32_t>& {
    wingScratch.clear();
    for (int32_t s = vfHead[u]; s != -1; s = slotNext[s]) {
      int32_t t = s / 3;
      if (triDeleted[t]) continue;
      if (corners[t * 3] == v || corners[t * 3 + 1] == v || corners[t * 3 + 2] == v)
        wingScratch.push_back(t);
    }
    return wingScratch;
  };

  auto thirdVertex = [&](int32_t t, int32_t u, int32_t v) {
    int32_t a = corners[t * 3], b = corners[t * 3 + 1], c = corners[t * 3 + 2];
    if (a != u && a != v) return a;
    if (b != u && b != v) return b;
    return c;
  };

  int64_t totalCollapses = 0;
  RegularizeRejectStats rejectStats;
  std::vector<int32_t> affectedScratch;

  // Attempt collapse of edge (u, v). Returns true if applied.
  auto tryCollapse = [&](int32_t u, int32_t v) -> bool {
    if (u == v) return false;

    if (frozenVert[u] || frozenVert[v]) {
      rejectStats.frozen++;
      return false;
    }

    auto& wings = trianglesSharingEdge(u, v);
    if (wings.size() != 2) {
      rejectStats.wingCount++;
      return false;
    }

    int32_t apexW1 = thirdVertex(wings[0], u, v);
    int32_t apexW2 = thirdVertex(wings[1], u, v);
    if (apexW1 == apexW2) {
      rejectStats.foldedApex++;
      return false;
    }

    // Two-tier gate selection.
    double w1Asp2 = triAspectSq(wings[0]);
    double w2Asp2 = triAspectSq(wings[1]);
    bool eitherExtreme = w1Asp2 > extremeAspect2 || w2Asp2 > extremeAspect2;
    bool bothExtreme = w1Asp2 > extremeAspect2 && w2Asp2 > extremeAspect2;
    double effMaxLenSq = eitherExtreme ? aggressiveMaxLenSqAllowed : baseMaxLenSqAllowed;
    double effNormalCos = bothExtreme ? aggressiveNormalDeltaCos : maxNormalDeltaCos;

    // Link condition — stamp v's neighbours, scan u's neighbours.
    stampGen++;
    for (int32_t s = vfHead[v]; s != -1; s = slotNext[s]) {
      int32_t t = s / 3;
      if (triDeleted[t]) continue;
      int32_t a = corners[t * 3], b = corners[t * 3 + 1], c = corners[t * 3 + 2];
      if (a != v) vertStamp[a] = stampGen;
      if (b != v) vertStamp[b] = stampGen;
      if (c != v) vertStamp[c] = stampGen;
    }
    for (int32_t s = vfHead[u]; s != -1; s = slotNext[s]) {
      int32_t t = s / 3;
      if (triDeleted[t]) continue;
      int32_t a = corners[t * 3], b = corners[t * 3 + 1], c = corners[t * 3 + 2];
      if (a != u && a != v && a != apexW1 && a != apexW2 && vertStamp[a] == stampGen) {
        rejectStats.linkCondition++;
        return false;
      }
      if (b != u && b != v && b != apexW1 && b != apexW2 && vertStamp[b] == stampGen) {
        rejectStats.linkCondition++;
        return false;
      }
      if (c != u && c != v && c != apexW1 && c != apexW2 && vertStamp[c] == stampGen) {
        rejectStats.linkCondition++;
        return false;
      }
    }

    // Merged position: midpoint
    double mx = (vertX[u] + vertX[v]) / 2;
    double my = (vertY[u] + vertY[v]) / 2;
    double mz = (vertZ[u] + vertZ[v]) / 2;

    // Affected triangles: all using u or v, excluding wings
    int32_t w0 = wings[0], w1 = wings[1];
    stampGen++;
    auto& affected = affectedScratch;
    affected.clear();
    for (int32_t s = vfHead[u]; s != -1; s = slotNext[s]) {
      int32_t t = s / 3;
      if (triDeleted[t] || t == w0 || t == w1) continue;
      if (triStamp[t] != stampGen) {
        triStamp[t] = stampGen;
        affected.push_back(t);
      }
    }
    for (int32_t s = vfHead[v]; s != -1; s = slotNext[s]) {
      int32_t t = s / 3;
      if (triDeleted[t] || t == w0 || t == w1) continue;
      if (triStamp[t] != stampGen) {
        triStamp[t] = stampGen;
        affected.push_back(t);
      }
    }

    // Validate every affected triangle's post-collapse state
    for (int32_t t : affected) {
      int32_t a = corners[t * 3], b = corners[t * 3 + 1], c = corners[t * 3 + 2];
      if (a == u || a == v) a = -1;
      if (b == u || b == v) b = -1;
      if (c == u || c == v) c = -1;
      double ax = a == -1 ? mx : vertX[a];
      double ay = a == -1 ? my : vertY[a];
      double az = a == -1 ? mz : vertZ[a];
      double bx = b == -1 ? mx : vertX[b];
      double by = b == -1 ? my : vertY[b];
      double bz = b == -1 ? mz : vertZ[b];
      double cx = c == -1 ? mx : vertX[c];
      double cy = c == -1 ? my : vertY[c];
      double cz = c == -1 ? mz : vertZ[c];

      double ab2 = (bx - ax) * (bx - ax) + (by - ay) * (by - ay) + (bz - az) * (bz - az);
      double bc2 = (cx - bx) * (cx - bx) + (cy - by) * (cy - by) + (cz - bz) * (cz - bz);
      double ca2 = (ax - cx) * (ax - cx) + (ay - cy) * (ay - cy) + (az - cz) * (az - cz);
      if (ab2 > effMaxLenSq || bc2 > effMaxLenSq || ca2 > effMaxLenSq) {
        rejectStats.edgeCap++;
        return false;
      }

      double e1x = bx - ax, e1y = by - ay, e1z = bz - az;
      double e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
      double nx = e1y * e2z - e1z * e2y;
      double ny = e1z * e2x - e1x * e2z;
      double nz = e1x * e2y - e1y * e2x;
      double nLen = std::sqrt(nx * nx + ny * ny + nz * nz);
      if (nLen <= 0) {
        rejectStats.degenerate++;
        return false;
      }
      double inv = 1 / nLen;
      double nux = nx * inv, nuy = ny * inv, nuz = nz * inv;
      // Gate against the ORIGINAL normal.
      double dot = nux * origNrmX[t] + nuy * origNrmY[t] + nuz * origNrmZ[t];
      if (dot < effNormalCos) {
        rejectStats.normalChange++;
        return false;
      }
    }

    // ── Apply the collapse ──
    vertX[u] = mx;
    vertY[u] = my;
    vertZ[u] = mz;

    triDeleted[w0] = 1;
    unlinkSlot(w0 * 3);
    unlinkSlot(w0 * 3 + 1);
    unlinkSlot(w0 * 3 + 2);
    triDeleted[w1] = 1;
    unlinkSlot(w1 * 3);
    unlinkSlot(w1 * 3 + 1);
    unlinkSlot(w1 * 3 + 2);

    // Substitute v→u in all remaining triangles using v.
    int32_t s = vfHead[v];
    while (s != -1) {
      int32_t ns = slotNext[s];
      unlinkSlot(s); // owner is still v here
      corners[s] = u;
      linkSlot(s); // now owned by u
      recomputeFaceNormal((size_t)(s / 3));
      s = ns;
    }
    // Recompute normals of all other affected triangles using u.
    for (int32_t s2 = vfHead[u]; s2 != -1; s2 = slotNext[s2]) {
      int32_t t = s2 / 3;
      if (triDeleted[t]) continue;
      recomputeFaceNormal((size_t)t);
    }
    return true;
  };

  for (int round = 0; round < maxRounds; round++) {
    // Candidate list (alive slivers), worst aspect first, stable ties.
    std::vector<int32_t> candT;
    std::vector<double> candA;
    for (size_t t = 0; t < triCount; t++) {
      if (triDeleted[t]) continue;
      int32_t a = corners[t * 3], b = corners[t * 3 + 1], c = corners[t * 3 + 2];
      double lAB2 = sqDist(a, b), lBC2 = sqDist(b, c), lCA2 = sqDist(c, a);
      double lmin2 = std::min({lAB2, lBC2, lCA2});
      if (lmin2 <= 0) continue;
      double aspect2 = triAspectSq(t);
      if (aspect2 < aspectThreshold * aspectThreshold) continue;
      candT.push_back((int32_t)t);
      candA.push_back(aspect2);
    }
    std::vector<size_t> order(candT.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t x, size_t y) { return candA[x] > candA[y]; });

    int64_t roundCollapses = 0;
    for (size_t ci = 0; ci < order.size(); ci++) {
      int32_t t = candT[order[ci]];
      if (triDeleted[t]) continue;
      int32_t a = corners[t * 3], b = corners[t * 3 + 1], c = corners[t * 3 + 2];
      // Try all three edges — shortest first.
      double el[3];
      int32_t eu[3], ev[3];
      el[0] = sqDist(a, b); eu[0] = a; ev[0] = b;
      el[1] = sqDist(b, c); eu[1] = b; ev[1] = c;
      el[2] = sqDist(c, a); eu[2] = c; ev[2] = a;
      int o0 = 0, o1 = 1, o2 = 2;
      if (el[o1] < el[o0]) std::swap(o0, o1);
      if (el[o2] < el[o1]) {
        std::swap(o1, o2);
        if (el[o1] < el[o0]) std::swap(o0, o1);
      }
      if (tryCollapse(eu[o0], ev[o0]) || tryCollapse(eu[o1], ev[o1]) ||
          tryCollapse(eu[o2], ev[o2])) {
        roundCollapses++;
      }
    }
    totalCollapses += roundCollapses;
    if (roundCollapses == 0) break;
  }

  // ── Compact: drop deleted tris, build output buffers ──
  size_t deleted = 0;
  for (size_t t = 0; t < triCount; t++)
    if (triDeleted[t]) deleted++;
  size_t survivingTriCount = triCount - deleted;

  RegularizeResult result;
  result.geometry.positions.resize(survivingTriCount * 9);
  result.faceParentId.resize(survivingTriCount);
  size_t oi = 0;
  for (size_t t = 0; t < triCount; t++) {
    if (triDeleted[t]) continue;
    int32_t a = corners[t * 3], b = corners[t * 3 + 1], c = corners[t * 3 + 2];
    float* op = result.geometry.positions.data() + oi * 9;
    op[0] = (float)vertX[a]; op[1] = (float)vertY[a]; op[2] = (float)vertZ[a];
    op[3] = (float)vertX[b]; op[4] = (float)vertY[b]; op[5] = (float)vertZ[b];
    op[6] = (float)vertX[c]; op[7] = (float)vertY[c]; op[8] = (float)vertZ[c];
    result.faceParentId[oi] = newParentId[t];
    oi++;
  }

  computeVertexNormals(result.geometry);

  // Carry through excludeWeight if input had it (precision masking pipeline).
  if (excludeWeight && !excludeWeight->empty()) {
    result.excludeWeight.resize(survivingTriCount * 3);
    size_t oj = 0;
    for (size_t t = 0; t < triCount; t++) {
      if (triDeleted[t]) continue;
      // Per-face exclusion was constant across the 3 vertices; take the first.
      float w = (*excludeWeight)[t * 3];
      result.excludeWeight[oj * 3] = w;
      result.excludeWeight[oj * 3 + 1] = w;
      result.excludeWeight[oj * 3 + 2] = w;
      oj++;
    }
  }

  result.collapseCount = totalCollapses;
  result.rejectStats = rejectStats;
  return result;
}

} // namespace core
