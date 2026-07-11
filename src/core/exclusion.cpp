#include "core/exclusion.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "core/mesh_index.h"

namespace core {

namespace {
constexpr double QUANT = 1e4;
constexpr double PI = 3.14159265358979323846;
} // namespace

AdjacencyData buildAdjacency(const Geometry& geometry) {
  const auto& pos = geometry.positions;
  size_t triCount = pos.size() / 9;

  AdjacencyData out;
  out.faceNormals.resize(triCount * 3);
  out.centroids.resize(triCount * 3);
  out.boundRadii.resize(triCount);

  for (size_t t = 0; t < triCount; t++) {
    size_t i = t * 9;
    double ax = pos[i], ay = pos[i + 1], az = pos[i + 2];
    double bx = pos[i + 3], by = pos[i + 4], bz = pos[i + 5];
    double cx = pos[i + 6], cy = pos[i + 7], cz = pos[i + 8];

    double e1x = bx - ax, e1y = by - ay, e1z = bz - az;
    double e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
    double fnx = e1y * e2z - e1z * e2y;
    double fny = e1z * e2x - e1x * e2z;
    double fnz = e1x * e2y - e1y * e2x;
    double len = std::sqrt(fnx * fnx + fny * fny + fnz * fnz);
    if (len > 0) { fnx /= len; fny /= len; fnz /= len; }

    out.faceNormals[t * 3] = (float)fnx;
    out.faceNormals[t * 3 + 1] = (float)fny;
    out.faceNormals[t * 3 + 2] = (float)fnz;

    double ccx = (ax + bx + cx) / 3;
    double ccy = (ay + by + cy) / 3;
    double ccz = (az + bz + cz) / 3;
    out.centroids[t * 3] = (float)ccx;
    out.centroids[t * 3 + 1] = (float)ccy;
    out.centroids[t * 3 + 2] = (float)ccz;
    double dA = (ax - ccx) * (ax - ccx) + (ay - ccy) * (ay - ccy) + (az - ccz) * (az - ccz);
    double dB = (bx - ccx) * (bx - ccx) + (by - ccy) * (by - ccy) + (bz - ccz) * (bz - ccz);
    double dC = (cx - ccx) * (cx - ccx) + (cy - ccy) * (cy - ccy) + (cz - ccz) * (cz - ccz);
    out.boundRadii[t] = (float)std::sqrt(std::max({dA, dB, dC}));
  }

  // Vertex dedup on the 1e4 grid, then edge → triangle list.
  QuantizedPointMap posToId(QUANT, std::min(triCount * 3, (size_t)1 << 22));
  uint32_t nextId = 0;
  std::vector<uint32_t> vertId(triCount * 3);
  for (size_t i = 0; i < triCount * 3; i++) {
    int32_t id = posToId.getOrSet(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2],
                                  (int32_t)nextId);
    if (posToId.inserted) nextId++;
    vertId[i] = (uint32_t)id;
  }

  auto numEdgeKey = [&](uint64_t a, uint64_t b) {
    return a < b ? a * nextId + b : b * nextId + a;
  };

  // Insertion-ordered edge map (JS Map semantics).
  std::unordered_map<uint64_t, std::vector<int32_t>> edgeMap;
  std::vector<uint64_t> edgeOrder;
  edgeMap.reserve(triCount * 3);
  edgeOrder.reserve(triCount * 3);
  const int edgePairs[6] = {0, 1, 0, 2, 1, 2};

  for (size_t t = 0; t < triCount; t++) {
    size_t base = t * 3;
    for (int e = 0; e < 6; e += 2) {
      uint64_t ek = numEdgeKey(vertId[base + edgePairs[e]], vertId[base + edgePairs[e + 1]]);
      auto [it, inserted] = edgeMap.try_emplace(ek);
      if (inserted) edgeOrder.push_back(ek);
      it->second.push_back((int32_t)t);
    }
  }

  out.adjacency.assign(triCount, {});

  for (uint64_t ek : edgeOrder) {
    const auto& tris = edgeMap[ek];
    if (tris.size() == 1) {
      out.openEdgeCount++;
      continue;
    }
    if (tris.size() > 2) out.nonManifoldEdgeCount++;
    int32_t a = tris[0], b = tris[1];
    double nAx = out.faceNormals[a * 3], nAy = out.faceNormals[a * 3 + 1], nAz = out.faceNormals[a * 3 + 2];
    double nBx = out.faceNormals[b * 3], nBy = out.faceNormals[b * 3 + 1], nBz = out.faceNormals[b * 3 + 2];
    double dot = std::max(-1.0, std::min(1.0, nAx * nBx + nAy * nBy + nAz * nBz));
    double angleDeg = std::acos(dot) * (180 / PI);
    out.adjacency[a].push_back({b, angleDeg});
    out.adjacency[b].push_back({a, angleDeg});
  }

  return out;
}

std::vector<int32_t> bucketFill(int32_t seedTriIdx, const AdjacencyData& adj,
                                double thresholdDeg) {
  std::vector<uint8_t> visited(adj.adjacency.size(), 0);
  std::vector<int32_t> queue;
  if (seedTriIdx >= 0 && (size_t)seedTriIdx < adj.adjacency.size())
    visited[seedTriIdx] = 1;
  queue.push_back(seedTriIdx);
  size_t head = 0;
  while (head < queue.size()) {
    int32_t cur = queue[head++];
    if (cur < 0 || (size_t)cur >= adj.adjacency.size()) continue;
    for (const AdjEntry& n : adj.adjacency[cur]) {
      if (!visited[n.neighbor] && n.angle <= thresholdDeg) {
        visited[n.neighbor] = 1;
        queue.push_back(n.neighbor);
      }
    }
  }
  return queue;
}

Geometry buildExclusionOverlayGeo(const Geometry& geometry,
                                  const std::vector<uint8_t>& faceMask,
                                  bool invert) {
  const auto& srcPos = geometry.positions;
  const auto& srcNrm = geometry.normals;
  size_t total = srcPos.size() / 9;
  bool hasNrm = !srcNrm.empty();

  size_t setSize = 0;
  for (size_t t = 0; t < total && t < faceMask.size(); t++)
    if (faceMask[t]) setSize++;
  size_t count = invert ? total - setSize : setSize;

  Geometry out;
  out.positions.resize(count * 9);
  if (hasNrm) out.normals.resize(count * 9);
  size_t dst = 0;
  for (size_t t = 0; t < total; t++) {
    bool inSet = t < faceMask.size() && faceMask[t];
    if (invert ? inSet : !inSet) continue;
    size_t src = t * 9;
    std::copy_n(srcPos.data() + src, 9, out.positions.data() + dst);
    if (hasNrm) std::copy_n(srcNrm.data() + src, 9, out.normals.data() + dst);
    dst += 9;
  }
  return out;
}

std::vector<float> buildFaceWeights(const Geometry& geometry,
                                    const std::vector<uint8_t>& excludedMask,
                                    bool invert) {
  size_t count = geometry.positions.size() / 3;
  std::vector<float> weights(count, invert ? 1.0f : 0.0f);
  size_t triCount = count / 3;
  for (size_t t = 0; t < triCount && t < excludedMask.size(); t++) {
    if (!excludedMask[t]) continue;
    float w = invert ? 0.0f : 1.0f;
    weights[t * 3] = w;
    weights[t * 3 + 1] = w;
    weights[t * 3 + 2] = w;
  }
  return weights;
}

} // namespace core
