#include "core/mesh_repair.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/jsmath.h"
#include "core/mesh_index.h"

namespace core {

namespace {

// JS numeric edge key a*2^32+b (exact in double for ids < 2^21) → uint64.
inline uint64_t ekey(int64_t a, int64_t b) {
  return a < b ? (uint64_t)a * 4294967296ull + (uint64_t)b
               : (uint64_t)b * 4294967296ull + (uint64_t)a;
}

} // namespace

EdgeDefects countEdgeDefects(const Geometry& geometry, double Q) {
  const auto& p = geometry.positions;
  size_t n = p.size() / 9;
  QuantizedPointMap vmap(Q, std::min(n * 3, (size_t)1 << 22));
  std::vector<int32_t> id(n * 3);
  for (size_t i = 0; i < n * 3; i++) {
    id[i] = vmap.getOrSet(p[i * 3], p[i * 3 + 1], p[i * 3 + 2], (int32_t)vmap.size());
  }
  std::unordered_map<uint64_t, int32_t> ec;
  ec.reserve(n * 3);
  for (size_t t = 0; t < n; t++) {
    int32_t a = id[t * 3], b = id[t * 3 + 1], c = id[t * 3 + 2];
    if (a == b || b == c || a == c) continue;
    int32_t tri[3] = {a, b, c};
    for (int e = 0; e < 3; e++) ec[ekey(tri[e], tri[(e + 1) % 3])]++;
  }
  EdgeDefects d;
  d.tris = (int64_t)n;
  for (const auto& [k, c] : ec) {
    if (c == 1) d.open++;
    else if (c > 2) d.nonManifold++;
  }
  return d;
}

int64_t countAreaSlivers(const Geometry& geometry) {
  const auto& p = geometry.positions;
  size_t n = p.size() / 9;
  int64_t s = 0;
  for (size_t t = 0; t < n; t++) {
    size_t b = t * 9;
    double ux = (double)p[b + 3] - p[b], uy = (double)p[b + 4] - p[b + 1], uz = (double)p[b + 5] - p[b + 2];
    double vx = (double)p[b + 6] - p[b], vy = (double)p[b + 7] - p[b + 1], vz = (double)p[b + 8] - p[b + 2];
    double c1 = uy * vz - uz * vy, c2 = uz * vx - ux * vz, c3 = ux * vy - uy * vx;
    double a2 = c1 * c1 + c2 * c2 + c3 * c3;
    if (a2 < 1e-24) s++;
  }
  return s;
}

Geometry resolveTJunctions(const Geometry& geometry,
                           const ResolveTJunctionOpts& opts) {
  const double Q = opts.weldQuant;
  const double onTol = opts.onSegTol;
  const int maxIters = opts.maxIters;
  const double onTol2 = onTol * onTol;

  const auto& pos = geometry.positions;
  size_t nTri = pos.size() / 9;

  // ── Weld at the export grid, SNAPPING coords onto that grid ───────────────
  QuantizedPointMap vmap(Q, std::min(nTri * 3, (size_t)1 << 22));
  std::vector<double> vx, vy, vz;
  std::vector<int32_t> vid(nTri * 3);
  for (size_t i = 0; i < nTri * 3; i++) {
    double x = pos[i * 3], y = pos[i * 3 + 1], z = pos[i * 3 + 2];
    int32_t id = vmap.getOrSet(x, y, z, (int32_t)vx.size());
    if (vmap.inserted) {
      vx.push_back(js::round(x * Q) / Q);
      vy.push_back(js::round(y * Q) / Q);
      vz.push_back(js::round(z * Q) / Q);
    }
    vid[i] = id;
  }

  // Faces as index triples, dropping index-degenerates and on-grid needles
  // (cross² < 1e-18) so the split pass can re-seal them.
  const double DEGEN_AREA2 = 1e-18;
  std::vector<std::array<int32_t, 3>> faces;
  faces.reserve(nTri);
  for (size_t t = 0; t < nTri; t++) {
    int32_t a = vid[t * 3], b = vid[t * 3 + 1], c = vid[t * 3 + 2];
    if (a == b || b == c || a == c) continue;
    double ux = vx[b] - vx[a], uy = vy[b] - vy[a], uz = vz[b] - vz[a];
    double wx = vx[c] - vx[a], wy = vy[c] - vy[a], wz = vz[c] - vz[a];
    double cx = uy * wz - uz * wy, cy = uz * wx - ux * wz, cz = ux * wy - uy * wx;
    if (cx * cx + cy * cy + cz * cz < DEGEN_AREA2) continue;
    faces.push_back({a, b, c});
  }

  for (int iter = 0; iter < maxIters; iter++) {
    // Edge → adjacent-face count, insertion-ordered like a JS Map.
    std::unordered_map<uint64_t, int32_t> eCount;
    std::vector<uint64_t> eOrder;
    eCount.reserve(faces.size() * 3);
    eOrder.reserve(faces.size() * 3);
    for (const auto& f : faces) {
      for (int e = 0; e < 3; e++) {
        uint64_t k = ekey(f[e], f[(e + 1) % 3]);
        auto [it, inserted] = eCount.try_emplace(k, 0);
        if (inserted) eOrder.push_back(k);
        it->second++;
      }
    }
    // Boundary vertices in JS Set insertion order.
    std::unordered_set<int32_t> bseen;
    std::vector<int32_t> bvArr;
    for (uint64_t k : eOrder) {
      if (eCount[k] != 1) continue;
      int32_t b = (int32_t)(k % 4294967296ull);
      int32_t a = (int32_t)(k / 4294967296ull);
      if (bseen.insert(a).second) bvArr.push_back(a);
      if (bseen.insert(b).second) bvArr.push_back(b);
    }
    if (bvArr.empty()) break;

    // For each boundary edge, find boundary vertices lying on its segment.
    struct Split {
      int32_t a, b;
      std::vector<int32_t> mids;
    };
    std::unordered_map<size_t, Split> splits;
    bool didSplit = false;
    for (size_t fi = 0; fi < faces.size(); fi++) {
      const auto& f = faces[fi];
      for (int e = 0; e < 3; e++) {
        int32_t a = f[e], b = f[(e + 1) % 3];
        auto it = eCount.find(ekey(a, b));
        if (it == eCount.end() || it->second != 1) continue; // boundary only
        double ax = vx[a], ay = vy[a], az = vz[a];
        double ex = vx[b] - ax, ey = vy[b] - ay, ez = vz[b] - az;
        double elen2 = ex * ex + ey * ey + ez * ez;
        if (elen2 < 1e-20) continue;
        std::vector<std::pair<double, int32_t>> found;
        for (int32_t c : bvArr) {
          if (c == a || c == b) continue;
          double cx = vx[c] - ax, cy = vy[c] - ay, cz = vz[c] - az;
          double tp = (cx * ex + cy * ey + cz * ez) / elen2;
          if (tp <= 1e-4 || tp >= 1 - 1e-4) continue; // strictly between A and B
          double px = cx - tp * ex, py = cy - tp * ey, pz = cz - tp * ez;
          if (px * px + py * py + pz * pz < onTol2) found.emplace_back(tp, c);
        }
        if (!found.empty()) {
          std::stable_sort(found.begin(), found.end(),
                           [](const auto& p, const auto& q) { return p.first < q.first; });
          Split sp;
          sp.a = a;
          sp.b = b;
          for (const auto& m : found) sp.mids.push_back(m.second);
          splits.emplace(fi, std::move(sp));
          didSplit = true;
          break; // one split site per face per pass; cascades via iteration
        }
      }
    }
    if (!didSplit) break;

    // Apply: replace each split face with a fan from its apex over the edge.
    std::vector<std::array<int32_t, 3>> next;
    next.reserve(faces.size() + splits.size() * 2);
    for (size_t fi = 0; fi < faces.size(); fi++) {
      auto it = splits.find(fi);
      if (it == splits.end()) {
        next.push_back(faces[fi]);
        continue;
      }
      const auto& f = faces[fi];
      const Split& sp = it->second;
      int32_t a = sp.a, b = sp.b;
      int32_t apex = (f[0] != a && f[0] != b) ? f[0]
                     : (f[1] != a && f[1] != b) ? f[1]
                                                : f[2];
      // Preserve winding: walk the base in the direction the face traverses it.
      bool dirAB = false;
      for (int e = 0; e < 3; e++) {
        if (f[e] == a && f[(e + 1) % 3] == b) {
          dirAB = true;
          break;
        }
      }
      std::vector<int32_t> seq;
      if (dirAB) {
        seq.push_back(a);
        seq.insert(seq.end(), sp.mids.begin(), sp.mids.end());
        seq.push_back(b);
      } else {
        seq.push_back(b);
        seq.insert(seq.end(), sp.mids.rbegin(), sp.mids.rend());
        seq.push_back(a);
      }
      for (size_t s = 0; s + 1 < seq.size(); s++)
        next.push_back({seq[s], seq[s + 1], apex});
    }
    faces = std::move(next);
  }

  // ── Rebuild non-indexed soup with flat normals ─────────────────────────────
  Geometry g;
  g.positions.resize(faces.size() * 9);
  g.normals.resize(faces.size() * 9);
  for (size_t i = 0; i < faces.size(); i++) {
    const auto& f = faces[i];
    double ax = vx[f[0]], ay = vy[f[0]], az = vz[f[0]];
    double bx = vx[f[1]], by = vy[f[1]], bz = vz[f[1]];
    double cx = vx[f[2]], cy = vy[f[2]], cz = vz[f[2]];
    g.positions[i * 9] = (float)ax; g.positions[i * 9 + 1] = (float)ay; g.positions[i * 9 + 2] = (float)az;
    g.positions[i * 9 + 3] = (float)bx; g.positions[i * 9 + 4] = (float)by; g.positions[i * 9 + 5] = (float)bz;
    g.positions[i * 9 + 6] = (float)cx; g.positions[i * 9 + 7] = (float)cy; g.positions[i * 9 + 8] = (float)cz;
    double ux = bx - ax, uy = by - ay, uz = bz - az;
    double vvx = cx - ax, vvy = cy - ay, vvz = cz - az;
    double nxx = uy * vvz - uz * vvy, nyy = uz * vvx - ux * vvz, nzz = ux * vvy - uy * vvx;
    double len = std::sqrt(nxx * nxx + nyy * nyy + nzz * nzz);
    if (len == 0) len = 1;
    nxx /= len; nyy /= len; nzz /= len;
    for (int k = 0; k < 3; k++) {
      g.normals[i * 9 + k * 3] = (float)nxx;
      g.normals[i * 9 + k * 3 + 1] = (float)nyy;
      g.normals[i * 9 + k * 3 + 2] = (float)nzz;
    }
  }
  return g;
}

} // namespace core
