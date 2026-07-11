#include "core/mesh_validation.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#include "core/mesh_index.h"

namespace core {

namespace {

// BFS component count over the triangle adjacency graph.
int64_t countShells(const std::vector<std::vector<AdjEntry>>& adjacency,
                    int64_t triCount) {
  std::vector<uint8_t> visited(triCount, 0);
  int64_t shellCount = 0;
  std::vector<int32_t> queue;
  for (int64_t seed = 0; seed < triCount; seed++) {
    if (visited[seed]) continue;
    shellCount++;
    queue.clear();
    queue.push_back((int32_t)seed);
    visited[seed] = 1;
    size_t head = 0;
    while (head < queue.size()) {
      int32_t cur = queue[head++];
      if ((size_t)cur >= adjacency.size()) continue;
      for (const AdjEntry& e : adjacency[cur]) {
        if (!visited[e.neighbor]) {
          visited[e.neighbor] = 1;
          queue.push_back(e.neighbor);
        }
      }
    }
  }
  return shellCount;
}

// Grid cell key — JS uses the string `${ix}_${iy}_${iz}`; any exact triple key
// is equivalent as long as insertion order is preserved by the caller.
struct CellKey {
  int64_t x, y, z;
  bool operator==(const CellKey& o) const {
    return x == o.x && y == o.y && z == o.z;
  }
};
struct CellKeyHash {
  size_t operator()(const CellKey& k) const {
    uint64_t h = (uint64_t)k.x * 0x9E3779B97F4A7C15ull;
    h ^= (uint64_t)k.y + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h ^= (uint64_t)k.z + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return (size_t)h;
  }
};

// SAT triangle-triangle intersection: 13 axes, then plane-straddle
// confirmation to reject touching/grazing/coplanar false positives.
bool trianglesIntersectSAT(const std::vector<float>& pos, int64_t tA,
                           int64_t tB) {
  const int64_t bA = tA * 9, bB = tB * 9;

  const double a0x = pos[bA], a0y = pos[bA + 1], a0z = pos[bA + 2];
  const double a1x = pos[bA + 3], a1y = pos[bA + 4], a1z = pos[bA + 5];
  const double a2x = pos[bA + 6], a2y = pos[bA + 7], a2z = pos[bA + 8];

  const double b0x = pos[bB], b0y = pos[bB + 1], b0z = pos[bB + 2];
  const double b1x = pos[bB + 3], b1y = pos[bB + 4], b1z = pos[bB + 5];
  const double b2x = pos[bB + 6], b2y = pos[bB + 7], b2z = pos[bB + 8];

  const double eA0x = a1x - a0x, eA0y = a1y - a0y, eA0z = a1z - a0z;
  const double eA1x = a2x - a1x, eA1y = a2y - a1y, eA1z = a2z - a1z;
  const double eA2x = a0x - a2x, eA2y = a0y - a2y, eA2z = a0z - a2z;

  const double eB0x = b1x - b0x, eB0y = b1y - b0y, eB0z = b1z - b0z;
  const double eB1x = b2x - b1x, eB1y = b2y - b1y, eB1z = b2z - b1z;
  const double eB2x = b0x - b2x, eB2y = b0y - b2y, eB2z = b0z - b2z;

  const double nAx = eA0y * eA2z - eA0z * eA2y;
  const double nAy = eA0z * eA2x - eA0x * eA2z;
  const double nAz = eA0x * eA2y - eA0y * eA2x;

  const double nBx = eB0y * eB2z - eB0z * eB2y;
  const double nBy = eB0z * eB2x - eB0x * eB2z;
  const double nBz = eB0x * eB2y - eB0y * eB2x;

  auto separated = [&](double ax, double ay, double az) {
    const double lenSq = ax * ax + ay * ay + az * az;
    if (lenSq < 1e-20) return false; // degenerate axis, skip

    const double pA0 = a0x * ax + a0y * ay + a0z * az;
    const double pA1 = a1x * ax + a1y * ay + a1z * az;
    const double pA2 = a2x * ax + a2y * ay + a2z * az;
    const double pB0 = b0x * ax + b0y * ay + b0z * az;
    const double pB1 = b1x * ax + b1y * ay + b1z * az;
    const double pB2 = b2x * ax + b2y * ay + b2z * az;

    const double minA = std::min({pA0, pA1, pA2});
    const double maxA = std::max({pA0, pA1, pA2});
    const double minB = std::min({pB0, pB1, pB2});
    const double maxB = std::max({pB0, pB1, pB2});

    const double eps = 1e-8 * std::max({std::abs(maxA), std::abs(maxB),
                                        std::abs(minA), std::abs(minB), 1.0});
    return maxA < minB - eps || maxB < minA - eps;
  };

  if (separated(nAx, nAy, nAz)) return false;
  if (separated(nBx, nBy, nBz)) return false;

  const double edgesA[3][3] = {{eA0x, eA0y, eA0z},
                               {eA1x, eA1y, eA1z},
                               {eA2x, eA2y, eA2z}};
  const double edgesB[3][3] = {{eB0x, eB0y, eB0z},
                               {eB1x, eB1y, eB1z},
                               {eB2x, eB2y, eB2z}};
  for (const auto& eA : edgesA) {
    for (const auto& eB : edgesB) {
      const double cx = eA[1] * eB[2] - eA[2] * eB[1];
      const double cy = eA[2] * eB[0] - eA[0] * eB[2];
      const double cz = eA[0] * eB[1] - eA[1] * eB[0];
      if (separated(cx, cy, cz)) return false;
    }
  }

  // No separating axis found — confirm both triangles genuinely straddle each
  // other's plane beyond a size-relative tolerance.
  const double nAlen = std::sqrt(nAx * nAx + nAy * nAy + nAz * nAz);
  const double nBlen = std::sqrt(nBx * nBx + nBy * nBy + nBz * nBz);
  if (nAlen < 1e-20 || nBlen < 1e-20) return false; // degenerate triangle

  const double dA = nAx * a0x + nAy * a0y + nAz * a0z;
  const double db0 = (nAx * b0x + nAy * b0y + nAz * b0z - dA) / nAlen;
  const double db1 = (nAx * b1x + nAy * b1y + nAz * b1z - dA) / nAlen;
  const double db2 = (nAx * b2x + nAy * b2y + nAz * b2z - dA) / nAlen;

  const double dB = nBx * b0x + nBy * b0y + nBz * b0z;
  const double da0 = (nBx * a0x + nBy * a0y + nBz * a0z - dB) / nBlen;
  const double da1 = (nBx * a1x + nBy * a1y + nBz * a1z - dB) / nBlen;
  const double da2 = (nBx * a2x + nBy * a2y + nBz * a2z - dB) / nBlen;

  const double maxEdgeA =
      std::max({eA0x * eA0x + eA0y * eA0y + eA0z * eA0z,
                eA1x * eA1x + eA1y * eA1y + eA1z * eA1z,
                eA2x * eA2x + eA2y * eA2y + eA2z * eA2z});
  const double maxEdgeB =
      std::max({eB0x * eB0x + eB0y * eB0y + eB0z * eB0z,
                eB1x * eB1x + eB1y * eB1y + eB1z * eB1z,
                eB2x * eB2x + eB2y * eB2y + eB2z * eB2z});
  const double eps = 1e-4 * std::sqrt(std::min(maxEdgeA, maxEdgeB));

  const double bMin = std::min({db0, db1, db2});
  const double bMax = std::max({db0, db1, db2});
  if (bMin > -eps || bMax < eps) return false; // B entirely on one side of A

  const double aMin = std::min({da0, da1, da2});
  const double aMax = std::max({da0, da1, da2});
  if (aMin > -eps || aMax < eps) return false; // A entirely on one side of B

  return true;
}

struct IntersectResult {
  bool aborted = false;
  int64_t count = 0;
  std::vector<int32_t> faces; // insertion order
};

IntersectResult findIntersectingTriangles(
    const Geometry& geometry, const std::function<bool()>& shouldAbort) {
  IntersectResult result;
  const std::vector<float>& pos = geometry.positions;
  const int64_t triCount = (int64_t)pos.size() / 9;

  // Quantized vertex ids: triangles sharing any vertex are topological
  // neighbors and must be skipped (SAT would flag them).
  const double QUANT = 1e4;
  QuantizedPointMap posToId(QUANT,
                            (size_t)std::min(triCount * 3, (int64_t)1 << 22));
  int32_t nextVId = 0;
  std::vector<uint32_t> triVerts(triCount * 3);
  for (int64_t t = 0; t < triCount; t++) {
    const int64_t b = t * 9;
    for (int v = 0; v < 3; v++) {
      const int64_t off = b + v * 3;
      int32_t id =
          posToId.getOrSet(pos[off], pos[off + 1], pos[off + 2], nextVId);
      if (posToId.inserted) nextVId++;
      triVerts[t * 3 + v] = (uint32_t)id;
    }
  }

  auto sharesVertex = [&](int64_t a, int64_t b) {
    const int64_t aBase = a * 3, bBase = b * 3;
    for (int i = 0; i < 3; i++) {
      const uint32_t vid = triVerts[aBase + i];
      if (vid == triVerts[bBase] || vid == triVerts[bBase + 1] ||
          vid == triVerts[bBase + 2])
        return true;
    }
    return false;
  };

  // Per-triangle AABB (float, like the JS Float32Arrays).
  std::vector<float> minX(triCount), minY(triCount), minZ(triCount);
  std::vector<float> maxX(triCount), maxY(triCount), maxZ(triCount);
  for (int64_t t = 0; t < triCount; t++) {
    const int64_t b = t * 9;
    const float ax = pos[b], ay = pos[b + 1], az = pos[b + 2];
    const float bx = pos[b + 3], by = pos[b + 4], bz = pos[b + 5];
    const float cx = pos[b + 6], cy = pos[b + 7], cz = pos[b + 8];
    minX[t] = std::min({ax, bx, cx});
    maxX[t] = std::max({ax, bx, cx});
    minY[t] = std::min({ay, by, cy});
    maxY[t] = std::max({ay, by, cy});
    minZ[t] = std::min({az, bz, cz});
    maxZ[t] = std::max({az, bz, cz});
  }

  // Cell size from median AABB extent.
  std::vector<float> extents(triCount);
  for (int64_t t = 0; t < triCount; t++) {
    extents[t] = std::max({maxX[t] - minX[t], maxY[t] - minY[t],
                           maxZ[t] - minZ[t]});
  }
  std::sort(extents.begin(), extents.end());
  const double cellSize =
      std::max((double)extents[triCount >> 1] * 2, 1e-6);
  const double invCell = 1 / cellSize;

  // Spatial grid; a triangle may span multiple cells. JS iterates the Map in
  // insertion order — keep cell lists in first-touch order.
  std::unordered_map<CellKey, uint32_t, CellKeyHash> cellIndex;
  std::vector<std::vector<int32_t>> cells;
  for (int64_t t = 0; t < triCount; t++) {
    const int64_t ix0 = (int64_t)std::floor(minX[t] * invCell);
    const int64_t iy0 = (int64_t)std::floor(minY[t] * invCell);
    const int64_t iz0 = (int64_t)std::floor(minZ[t] * invCell);
    const int64_t ix1 = (int64_t)std::floor(maxX[t] * invCell);
    const int64_t iy1 = (int64_t)std::floor(maxY[t] * invCell);
    const int64_t iz1 = (int64_t)std::floor(maxZ[t] * invCell);
    for (int64_t ix = ix0; ix <= ix1; ix++) {
      for (int64_t iy = iy0; iy <= iy1; iy++) {
        for (int64_t iz = iz0; iz <= iz1; iz++) {
          auto [it, isNew] =
              cellIndex.try_emplace({ix, iy, iz}, (uint32_t)cells.size());
          if (isNew) cells.emplace_back();
          cells[it->second].push_back((int32_t)t);
        }
      }
    }
  }

  // Narrow phase over same-cell candidate pairs.
  std::unordered_set<uint64_t> testedPairs;
  std::unordered_set<int32_t> faceSeen;
  int64_t pairsTested = 0;

  for (const std::vector<int32_t>& tris : cells) {
    for (size_t i = 0; i < tris.size(); i++) {
      for (size_t j = i + 1; j < tris.size(); j++) {
        const int32_t tA = tris[i], tB = tris[j];
        const int64_t a = std::min(tA, tB), b = std::max(tA, tB);
        const uint64_t pairKey = (uint64_t)a * (uint64_t)triCount + (uint64_t)b;

        if (!testedPairs.insert(pairKey).second) continue;

        if (sharesVertex(a, b)) continue;

        if (minX[a] > maxX[b] || minX[b] > maxX[a] || minY[a] > maxY[b] ||
            minY[b] > maxY[a] || minZ[a] > maxZ[b] || minZ[b] > maxZ[a])
          continue;

        if (trianglesIntersectSAT(pos, a, b)) {
          result.count++;
          if (faceSeen.insert((int32_t)a).second)
            result.faces.push_back((int32_t)a);
          if (faceSeen.insert((int32_t)b).second)
            result.faces.push_back((int32_t)b);
        }

        if (++pairsTested % 10000 == 0 && shouldAbort && shouldAbort()) {
          result.aborted = true;
          return result;
        }
      }
    }
  }

  return result;
}

struct OverlapResult {
  bool aborted = false;
  int64_t count = 0;
  std::vector<int32_t> faces; // insertion order
};

// Vertex key = raw float32 bit patterns (exact, no quantization).
using VertBits = std::array<uint32_t, 3>;
using TriKey = std::array<uint32_t, 9>;
struct TriKeyHash {
  size_t operator()(const TriKey& k) const {
    uint64_t h = 0xCBF29CE484222325ull;
    for (uint32_t v : k) {
      h ^= v;
      h *= 0x100000001B3ull;
    }
    return (size_t)h;
  }
};

OverlapResult findOverlappingTriangles(const Geometry& geometry,
                                       const std::function<bool()>& shouldAbort) {
  OverlapResult result;
  const std::vector<float>& pos = geometry.positions;
  const int64_t triCount = (int64_t)pos.size() / 9;

  std::unordered_map<TriKey, int32_t, TriKeyHash> triHashMap;
  triHashMap.reserve((size_t)triCount);
  std::unordered_set<int32_t> faceSeen;

  for (int64_t t = 0; t < triCount; t++) {
    const int64_t b = t * 9;
    std::array<VertBits, 3> verts;
    for (int v = 0; v < 3; v++) {
      verts[v] = {std::bit_cast<uint32_t>(pos[b + v * 3]),
                  std::bit_cast<uint32_t>(pos[b + v * 3 + 1]),
                  std::bit_cast<uint32_t>(pos[b + v * 3 + 2])};
    }
    // Winding-agnostic canonical key. (The JS sorts decimal strings; any
    // deterministic total order yields the same duplicate detection.)
    std::sort(verts.begin(), verts.end());
    TriKey key = {verts[0][0], verts[0][1], verts[0][2],
                  verts[1][0], verts[1][1], verts[1][2],
                  verts[2][0], verts[2][1], verts[2][2]};
    auto [it, isNew] = triHashMap.try_emplace(key, (int32_t)t);
    if (!isNew) {
      if (faceSeen.insert(it->second).second)
        result.faces.push_back(it->second);
      if (faceSeen.insert((int32_t)t).second)
        result.faces.push_back((int32_t)t);
    }

    if (t % 50000 == 0 && t > 0 && shouldAbort && shouldAbort()) {
      result.aborted = true;
      return result;
    }
  }

  result.count = (int64_t)result.faces.size();
  return result;
}

} // namespace

FastDiagnostics runFastDiagnostics(const AdjacencyData& adjData,
                                   int64_t triCount) {
  FastDiagnostics d;
  d.openEdges = adjData.openEdgeCount;
  d.nonManifoldEdges = adjData.nonManifoldEdgeCount;
  d.shellCount = countShells(adjData.adjacency, triCount);
  return d;
}

ExpensiveDiagnostics runExpensiveDiagnostics(
    const Geometry& geometry, const std::function<bool()>& shouldAbort) {
  ExpensiveDiagnostics d;

  OverlapResult overlap = findOverlappingTriangles(geometry, shouldAbort);
  if (overlap.aborted) {
    d.aborted = true;
    return d;
  }

  IntersectResult intersect = findIntersectingTriangles(geometry, shouldAbort);
  if (intersect.aborted) {
    d.aborted = true;
    return d;
  }

  d.intersectingPairs = intersect.count;
  d.intersectFaces = std::move(intersect.faces);
  d.overlappingPairs = overlap.count;
  d.overlapFaces = std::move(overlap.faces);
  return d;
}

EdgeHighlightPositions getEdgePositions(const Geometry& geometry) {
  const std::vector<float>& pos = geometry.positions;
  const int64_t vertCount = (int64_t)pos.size() / 3;
  const int64_t triCount = vertCount / 3;
  const double QUANT = 1e4;

  // Vertex dedup (same approach as buildAdjacency).
  QuantizedPointMap posToId(QUANT,
                            (size_t)std::min(triCount * 3, (int64_t)1 << 22));
  int32_t nextId = 0;
  std::vector<uint32_t> vertId(vertCount);
  for (int64_t i = 0; i < vertCount; i++) {
    int32_t id = posToId.getOrSet(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2],
                                  nextId);
    if (posToId.inserted) nextId++;
    vertId[i] = (uint32_t)id;
  }

  auto numEdgeKey = [&](uint64_t a, uint64_t b) {
    return a < b ? a * (uint64_t)nextId + b : b * (uint64_t)nextId + a;
  };
  static const int edgePairs[6] = {0, 1, 0, 2, 1, 2};

  // Insertion-ordered edge table: count + first-occurrence vertex indices.
  std::unordered_map<uint64_t, uint32_t> edgeIndex;
  struct EdgeEntry {
    int64_t count;
    int64_t v0, v1;
  };
  std::vector<EdgeEntry> edges;
  for (int64_t t = 0; t < triCount; t++) {
    const int64_t base = t * 3;
    for (int e = 0; e < 6; e += 2) {
      const int64_t vi0 = base + edgePairs[e], vi1 = base + edgePairs[e + 1];
      const uint64_t ek = numEdgeKey(vertId[vi0], vertId[vi1]);
      auto [it, isNew] = edgeIndex.try_emplace(ek, (uint32_t)edges.size());
      if (isNew) edges.push_back({1, vi0, vi1});
      else edges[it->second].count++;
    }
  }

  EdgeHighlightPositions out;
  for (const EdgeEntry& e : edges) {
    std::vector<float>* list = nullptr;
    if (e.count == 1) list = &out.open;
    else if (e.count > 2) list = &out.nonManifold;
    else continue;
    list->push_back(pos[e.v0 * 3]);
    list->push_back(pos[e.v0 * 3 + 1]);
    list->push_back(pos[e.v0 * 3 + 2]);
    list->push_back(pos[e.v1 * 3]);
    list->push_back(pos[e.v1 * 3 + 1]);
    list->push_back(pos[e.v1 * 3 + 2]);
  }
  return out;
}

std::vector<uint32_t> getShellAssignments(const AdjacencyData& adjData,
                                          int64_t triCount) {
  std::vector<uint32_t> shellId(triCount, 0);
  std::vector<uint8_t> visited(triCount, 0);
  uint32_t nextShell = 0;
  std::vector<int32_t> queue;
  for (int64_t seed = 0; seed < triCount; seed++) {
    if (visited[seed]) continue;
    const uint32_t id = nextShell++;
    queue.clear();
    queue.push_back((int32_t)seed);
    visited[seed] = 1;
    shellId[seed] = id;
    size_t head = 0;
    while (head < queue.size()) {
      int32_t cur = queue[head++];
      if ((size_t)cur >= adjData.adjacency.size()) continue;
      for (const AdjEntry& e : adjData.adjacency[cur]) {
        if (!visited[e.neighbor]) {
          visited[e.neighbor] = 1;
          shellId[e.neighbor] = id;
          queue.push_back(e.neighbor);
        }
      }
    }
  }
  return shellId;
}

} // namespace core
