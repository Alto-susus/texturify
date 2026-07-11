#include "core/subdivision.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "core/mesh_index.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace core {

namespace {

// 10 µm vertex-dedup cells — see the grid-policy table in reference/CONTEXT.md.
constexpr double QUANTISE = 1e5;
constexpr double PI = 3.14159265358979323846;

// Growable vertex store shared by the indexers (which build it) and the
// subdivision passes (which append midpoint vertices).
struct VertStore {
  size_t count = 0;
  std::vector<double> pos;  // xyz per vertex
  std::vector<double> nrm;  // xyz per vertex
  std::vector<double> wgt;  // optional exclusion weights
  std::vector<int32_t> canon; // optional canonical position ids
  bool hasWgt = false;
  bool hasCanon = false;

  void init(size_t initialCap, bool weights, bool canonIds) {
    pos.resize(initialCap * 3);
    nrm.resize(initialCap * 3);
    hasWgt = weights;
    hasCanon = canonIds;
    if (weights) wgt.resize(initialCap);
    if (canonIds) canon.resize(initialCap);
  }
  size_t cap() const { return pos.size() / 3; }
  void grow() {
    size_t newCap = cap() * 2;
    pos.resize(newCap * 3);
    nrm.resize(newCap * 3);
    if (hasWgt) wgt.resize(newCap);
    if (hasCanon) canon.resize(newCap);
  }
};

struct Indexed {
  VertStore verts;
  std::vector<uint32_t> indices;
  QuantizedPointMap* posCanonMap = nullptr; // owned separately in accurate mode
};

inline double edgeLenSq(const std::vector<double>& pos, uint32_t a, uint32_t b) {
  double dx = pos[a * 3] - pos[b * 3];
  double dy = pos[a * 3 + 1] - pos[b * 3 + 1];
  double dz = pos[a * 3 + 2] - pos[b * 3 + 2];
  return dx * dx + dy * dy + dz * dz;
}

uint32_t getMidpoint(VertStore& verts, QuantizedPointMap& cache, uint32_t a,
                     uint32_t b, QuantizedPointMap* posCanonMap) {
  uint32_t lo = a < b ? a : b, hi = a < b ? b : a;
  int32_t cached = cache.get(lo, hi, 0);
  if (cached != -1) return (uint32_t)cached;

  // Midpoint position
  double mx = (verts.pos[a * 3] + verts.pos[b * 3]) / 2;
  double my = (verts.pos[a * 3 + 1] + verts.pos[b * 3 + 1]) / 2;
  double mz = (verts.pos[a * 3 + 2] + verts.pos[b * 3 + 2]) / 2;

  // Midpoint normal (average + normalise)
  double nx = verts.nrm[a * 3] + verts.nrm[b * 3];
  double ny = verts.nrm[a * 3 + 1] + verts.nrm[b * 3 + 1];
  double nz = verts.nrm[a * 3 + 2] + verts.nrm[b * 3 + 2];
  double nl = std::sqrt(nx * nx + ny * ny + nz * nz);
  if (nl == 0) nl = 1;

  size_t idx = verts.count;
  if (idx == verts.cap()) verts.grow();
  verts.pos[idx * 3] = mx;
  verts.pos[idx * 3 + 1] = my;
  verts.pos[idx * 3 + 2] = mz;
  verts.nrm[idx * 3] = nx / nl;
  verts.nrm[idx * 3 + 1] = ny / nl;
  verts.nrm[idx * 3 + 2] = nz / nl;
  if (verts.hasWgt) verts.wgt[idx] = (verts.wgt[a] + verts.wgt[b]) / 2;
  if (verts.hasCanon)
    verts.canon[idx] = posCanonMap->getOrSet(mx, my, mz, (int32_t)idx);
  verts.count = idx + 1;

  cache.getOrSet(lo, hi, 0, (int32_t)idx);
  return (uint32_t)idx;
}

struct PassResult {
  std::vector<uint32_t> newIndices;
  std::vector<uint8_t> newFaceExcluded;
  std::vector<int32_t> newFaceParentId;
  bool changed = false;
  bool capped = false;
};

PassResult subdividePass(VertStore& verts, const std::vector<uint32_t>& indices,
                         double maxEdgeLength, int64_t safetyCap,
                         const std::vector<uint8_t>* faceExcluded,
                         QuantizedPointMap* posCanonMap,
                         const std::vector<int32_t>* faceParentId) {
  double maxSq = maxEdgeLength * maxEdgeLength;
  // Midpoint cache keyed by the RAW (unordered) parent-vertex pair.
  QuantizedPointMap midCache(1, 1 << 16);

  const std::vector<double>& positions = verts.pos;
  // NOTE: access verts.canon through the store, never a cached data()
  // pointer — getMidpoint grows the store mid-pass and reallocates it.
  const bool hasCanon = verts.hasCanon;
  auto canonOf = [&](uint32_t i) {
    return hasCanon ? verts.canon[i] : (int32_t)i;
  };

  // Position-canonical edge keys in accurate mode; index keys in fast mode.
  QuantizedPointMap splitEdges(1, 1 << 16);
  auto markEdge = [&](uint32_t a, uint32_t b) {
    int32_t u = canonOf(a), v = canonOf(b);
    if (u < v) splitEdges.getOrSet(u, v, 0, 1);
    else splitEdges.getOrSet(v, u, 0, 1);
  };
  auto isMarked = [&](uint32_t a, uint32_t b) {
    int32_t u = canonOf(a), v = canonOf(b);
    return (u < v ? splitEdges.get(u, v, 0) : splitEdges.get(v, u, 0)) != -1;
  };

  // ── Step 1: globally mark edges that need splitting ──────────────────────
  // Excluded triangles never mark their own edges; boundary edges are still
  // marked by the included neighbour (T-junction-free).
  for (size_t t = 0; t < indices.size(); t += 3) {
    if (faceExcluded && (*faceExcluded)[t / 3]) continue;
    uint32_t a = indices[t], b = indices[t + 1], c = indices[t + 2];
    if (edgeLenSq(positions, a, b) > maxSq) markEdge(a, b);
    if (edgeLenSq(positions, b, c) > maxSq) markEdge(b, c);
    if (edgeLenSq(positions, c, a) > maxSq) markEdge(c, a);
  }

  PassResult r;
  if (splitEdges.size() == 0) {
    r.newIndices = indices;
    if (faceExcluded) r.newFaceExcluded = *faceExcluded;
    if (faceParentId) r.newFaceParentId = *faceParentId;
    r.changed = false;
    return r;
  }

  // ── Step 1.5: predict the post-split triangle count ───────────────────────
  // If the prediction exceeds the cap, abort the ENTIRE pass — partial splits
  // would leave T-junctions.
  int64_t predictedTris = 0;
  for (size_t t = 0; t < indices.size(); t += 3) {
    uint32_t a = indices[t], b = indices[t + 1], c = indices[t + 2];
    int n = (isMarked(a, b) ? 1 : 0) + (isMarked(b, c) ? 1 : 0) +
            (isMarked(c, a) ? 1 : 0);
    predictedTris += n == 0 ? 1 : n + 1; // 0→1, 1→2, 2→3, 3→4
  }
  if (predictedTris > safetyCap) {
    r.newIndices = indices;
    if (faceExcluded) r.newFaceExcluded = *faceExcluded;
    if (faceParentId) r.newFaceParentId = *faceParentId;
    r.changed = false;
    r.capped = true;
    return r;
  }

  // ── Step 2: rebuild index list ────────────────────────────────────────────
  r.newIndices.resize((size_t)predictedTris * 3);
  if (faceExcluded) r.newFaceExcluded.resize((size_t)predictedTris);
  if (faceParentId) r.newFaceParentId.resize((size_t)predictedTris);
  auto* nextIndices = r.newIndices.data();
  size_t wi = 0; // vertex-slot write cursor
  size_t fi = 0; // face write cursor

  for (size_t t = 0; t < indices.size(); t += 3) {
    uint32_t a = indices[t], b = indices[t + 1], c = indices[t + 2];
    size_t fIdx = t / 3;
    uint8_t excl = faceExcluded ? (*faceExcluded)[fIdx] : 0;
    int32_t pid = faceParentId ? (*faceParentId)[fIdx] : 0;
    bool sAB = isMarked(a, b);
    bool sBC = isMarked(b, c);
    bool sCA = isMarked(c, a);
    int n = (sAB ? 1 : 0) + (sBC ? 1 : 0) + (sCA ? 1 : 0);

    if (n == 0) {
      // 0-split: keep triangle
      nextIndices[wi++] = a; nextIndices[wi++] = b; nextIndices[wi++] = c;
      if (faceExcluded) r.newFaceExcluded[fi] = excl;
      if (faceParentId) r.newFaceParentId[fi] = pid;
      fi++;
    } else if (n == 3) {
      // 3-split: 1→4 regular midpoint subdivision
      uint32_t mAB = getMidpoint(verts, midCache, a, b, posCanonMap);
      uint32_t mBC = getMidpoint(verts, midCache, b, c, posCanonMap);
      uint32_t mCA = getMidpoint(verts, midCache, c, a, posCanonMap);
      nextIndices[wi++] = a;   nextIndices[wi++] = mAB; nextIndices[wi++] = mCA;
      nextIndices[wi++] = mAB; nextIndices[wi++] = b;   nextIndices[wi++] = mBC;
      nextIndices[wi++] = mCA; nextIndices[wi++] = mBC; nextIndices[wi++] = c;
      nextIndices[wi++] = mAB; nextIndices[wi++] = mBC; nextIndices[wi++] = mCA;
      for (int k = 0; k < 4; k++) {
        if (faceExcluded) r.newFaceExcluded[fi] = excl;
        if (faceParentId) r.newFaceParentId[fi] = pid;
        fi++;
      }
    } else if (n == 1) {
      // 1-split: bisect the one marked edge → 2 sub-triangles
      if (sAB) {
        uint32_t m = getMidpoint(verts, midCache, a, b, posCanonMap);
        nextIndices[wi++] = a; nextIndices[wi++] = m; nextIndices[wi++] = c;
        nextIndices[wi++] = m; nextIndices[wi++] = b; nextIndices[wi++] = c;
      } else if (sBC) {
        uint32_t m = getMidpoint(verts, midCache, b, c, posCanonMap);
        nextIndices[wi++] = a; nextIndices[wi++] = b; nextIndices[wi++] = m;
        nextIndices[wi++] = a; nextIndices[wi++] = m; nextIndices[wi++] = c;
      } else { // sCA
        uint32_t m = getMidpoint(verts, midCache, c, a, posCanonMap);
        nextIndices[wi++] = a; nextIndices[wi++] = b; nextIndices[wi++] = m;
        nextIndices[wi++] = m; nextIndices[wi++] = b; nextIndices[wi++] = c;
      }
      for (int k = 0; k < 2; k++) {
        if (faceExcluded) r.newFaceExcluded[fi] = excl;
        if (faceParentId) r.newFaceParentId[fi] = pid;
        fi++;
      }
    } else {
      // 2-split: 3 sub-triangles, fan from the untouched-edge vertex
      if (!sAB) { // sBC + sCA: fan from C
        uint32_t mBC = getMidpoint(verts, midCache, b, c, posCanonMap);
        uint32_t mCA = getMidpoint(verts, midCache, c, a, posCanonMap);
        nextIndices[wi++] = a;   nextIndices[wi++] = b;   nextIndices[wi++] = mBC;
        nextIndices[wi++] = a;   nextIndices[wi++] = mBC; nextIndices[wi++] = mCA;
        nextIndices[wi++] = c;   nextIndices[wi++] = mCA; nextIndices[wi++] = mBC;
      } else if (!sBC) { // sAB + sCA: fan from A
        uint32_t mAB = getMidpoint(verts, midCache, a, b, posCanonMap);
        uint32_t mCA = getMidpoint(verts, midCache, c, a, posCanonMap);
        nextIndices[wi++] = a;   nextIndices[wi++] = mAB; nextIndices[wi++] = mCA;
        nextIndices[wi++] = mAB; nextIndices[wi++] = b;   nextIndices[wi++] = c;
        nextIndices[wi++] = mAB; nextIndices[wi++] = c;   nextIndices[wi++] = mCA;
      } else { // sAB + sBC: fan from B
        uint32_t mAB = getMidpoint(verts, midCache, a, b, posCanonMap);
        uint32_t mBC = getMidpoint(verts, midCache, b, c, posCanonMap);
        nextIndices[wi++] = b;   nextIndices[wi++] = mBC; nextIndices[wi++] = mAB;
        nextIndices[wi++] = a;   nextIndices[wi++] = mAB; nextIndices[wi++] = mBC;
        nextIndices[wi++] = a;   nextIndices[wi++] = mBC; nextIndices[wi++] = c;
      }
      for (int k = 0; k < 3; k++) {
        if (faceExcluded) r.newFaceExcluded[fi] = excl;
        if (faceParentId) r.newFaceParentId[fi] = pid;
        fi++;
      }
    }
  }

  r.changed = true;
  return r;
}

void normalizeStoreNormals(VertStore& verts) {
  auto& nrm = verts.nrm;
  for (size_t i = 0; i < verts.count; i++) {
    double nx = nrm[i * 3], ny = nrm[i * 3 + 1], nz = nrm[i * 3 + 2];
    double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len == 0) len = 1;
    nrm[i * 3] = nx / len;
    nrm[i * 3 + 1] = ny / len;
    nrm[i * 3 + 2] = nz / len;
  }
}

// ── Fast non-indexed → indexed (preview path) ───────────────────────────────
void toIndexedFast(const Geometry& geometry, const std::vector<float>* weights,
                   VertStore& verts, std::vector<uint32_t>& indices) {
  const auto& pos = geometry.positions;
  const auto& nrmA = geometry.normals;
  size_t n = pos.size() / 3;
  QuantizedPointMap vertMap(QUANTISE, std::min(n, (size_t)1 << 22));
  indices.resize(n);
  verts.init(std::max<size_t>(16, std::min<size_t>((size_t)1 << 16, n)),
             weights != nullptr, false);

  bool hasNrm = !nrmA.empty();
  for (size_t i = 0; i < n; i++) {
    double px = pos[i * 3], py = pos[i * 3 + 1], pz = pos[i * 3 + 2];
    double nx = hasNrm ? nrmA[i * 3] : 0;
    double ny = hasNrm ? nrmA[i * 3 + 1] : 0;
    double nz = hasNrm ? nrmA[i * 3 + 2] : 1;

    int32_t idx = vertMap.getOrSet(px, py, pz, (int32_t)verts.count);
    if (vertMap.inserted) {
      if (verts.count == verts.cap()) verts.grow();
      verts.pos[idx * 3] = px; verts.pos[idx * 3 + 1] = py; verts.pos[idx * 3 + 2] = pz;
      verts.nrm[idx * 3] = nx; verts.nrm[idx * 3 + 1] = ny; verts.nrm[idx * 3 + 2] = nz;
      if (verts.hasWgt) verts.wgt[idx] = (*weights)[i];
      verts.count++;
    } else {
      verts.nrm[idx * 3] += nx;
      verts.nrm[idx * 3 + 1] += ny;
      verts.nrm[idx * 3 + 2] += nz;
      if (verts.hasWgt && (*weights)[i] > verts.wgt[idx]) verts.wgt[idx] = (*weights)[i];
    }
    indices[i] = (uint32_t)idx;
  }

  normalizeStoreNormals(verts);
}

// ── Non-indexed → indexed conversion (export path) ───────────────────────────
struct Cluster {
  int32_t idx;
  double fnU[3];
};

void toIndexed(const Geometry& geometry, const std::vector<float>* weights,
               VertStore& verts, std::vector<uint32_t>& indices,
               QuantizedPointMap& posCanonMap) {
  const auto& posA = geometry.positions;
  size_t n = posA.size() / 3;

  // Per-face normals (unit + raw cross product), stored as float32 exactly
  // like the JS Float32Arrays — the rounding participates in downstream sums.
  std::vector<float> faceNrmUnit(n * 3), faceNrmRaw(n * 3);
  for (size_t t = 0; t < n; t += 3) {
    double ax = posA[t * 3], ay = posA[t * 3 + 1], az = posA[t * 3 + 2];
    double bx = posA[(t + 1) * 3], by = posA[(t + 1) * 3 + 1], bz = posA[(t + 1) * 3 + 2];
    double cx = posA[(t + 2) * 3], cy = posA[(t + 2) * 3 + 1], cz = posA[(t + 2) * 3 + 2];
    double e1x = bx - ax, e1y = by - ay, e1z = bz - az;
    double e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
    double rx = e1y * e2z - e1z * e2y;
    double ry = e1z * e2x - e1x * e2z;
    double rz = e1x * e2y - e1y * e2x;
    double len = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (len == 0) len = 1;
    float ux = (float)(rx / len), uy = (float)(ry / len), uz = (float)(rz / len);
    for (int v = 0; v < 3; v++) {
      faceNrmUnit[(t + v) * 3] = ux;
      faceNrmUnit[(t + v) * 3 + 1] = uy;
      faceNrmUnit[(t + v) * 3 + 2] = uz;
      faceNrmRaw[(t + v) * 3] = (float)rx;
      faceNrmRaw[(t + v) * 3 + 1] = (float)ry;
      faceNrmRaw[(t + v) * 3 + 2] = (float)rz;
    }
  }

  // Merge vertices, splitting at sharp dihedral edges (>30°).
  const double SHARP_COS = std::cos(30 * PI / 180);

  indices.resize(n);
  verts.init(std::max<size_t>(16, std::min<size_t>((size_t)1 << 16, n)),
             weights != nullptr, true);
  std::unordered_map<int32_t, std::vector<Cluster>> clustersByCanon;

  for (size_t i = 0; i < n; i++) {
    double px = posA[i * 3], py = posA[i * 3 + 1], pz = posA[i * 3 + 2];
    double fnUx = faceNrmUnit[i * 3], fnUy = faceNrmUnit[i * 3 + 1], fnUz = faceNrmUnit[i * 3 + 2];
    double fnRx = faceNrmRaw[i * 3], fnRy = faceNrmRaw[i * 3 + 1], fnRz = faceNrmRaw[i * 3 + 2];

    // The first vertex at a new position becomes its canonical ID.
    int32_t canonId = posCanonMap.getOrSet(px, py, pz, (int32_t)verts.count);
    std::vector<Cluster>* clusters = nullptr;
    if (!posCanonMap.inserted) {
      auto it = clustersByCanon.find(canonId);
      if (it != clustersByCanon.end()) clusters = &it->second;
    }
    if (clusters) {
      bool matched = false;
      for (Cluster& cl : *clusters) {
        double dot = cl.fnU[0] * fnUx + cl.fnU[1] * fnUy + cl.fnU[2] * fnUz;
        if (dot >= SHARP_COS) {
          // Same smooth group — accumulate area-weighted face normal
          int32_t idx = cl.idx;
          verts.nrm[idx * 3] += fnRx;
          verts.nrm[idx * 3 + 1] += fnRy;
          verts.nrm[idx * 3 + 2] += fnRz;
          if (verts.hasWgt && (*weights)[i] > verts.wgt[idx]) verts.wgt[idx] = (*weights)[i];
          // Update the cluster representative to the running average so
          // gradual curvature stays in one cluster.
          cl.fnU[0] += fnUx;
          cl.fnU[1] += fnUy;
          cl.fnU[2] += fnUz;
          double rl = std::sqrt(cl.fnU[0] * cl.fnU[0] + cl.fnU[1] * cl.fnU[1] +
                                cl.fnU[2] * cl.fnU[2]);
          if (rl == 0) rl = 1;
          cl.fnU[0] /= rl; cl.fnU[1] /= rl; cl.fnU[2] /= rl;
          indices[i] = (uint32_t)idx;
          matched = true;
          break;
        }
      }
      if (!matched) {
        // New cluster at this position (sharp-edge split)
        size_t idx = verts.count;
        if (idx == verts.cap()) verts.grow();
        verts.pos[idx * 3] = px; verts.pos[idx * 3 + 1] = py; verts.pos[idx * 3 + 2] = pz;
        verts.nrm[idx * 3] = fnRx; verts.nrm[idx * 3 + 1] = fnRy; verts.nrm[idx * 3 + 2] = fnRz;
        if (verts.hasWgt) verts.wgt[idx] = (*weights)[i];
        verts.canon[idx] = canonId;
        verts.count++;
        clusters->push_back({(int32_t)idx, {fnUx, fnUy, fnUz}});
        indices[i] = (uint32_t)idx;
      }
    } else {
      size_t idx = verts.count; // == canonId (just inserted above)
      if (idx == verts.cap()) verts.grow();
      verts.pos[idx * 3] = px; verts.pos[idx * 3 + 1] = py; verts.pos[idx * 3 + 2] = pz;
      verts.nrm[idx * 3] = fnRx; verts.nrm[idx * 3 + 1] = fnRy; verts.nrm[idx * 3 + 2] = fnRz;
      if (verts.hasWgt) verts.wgt[idx] = (*weights)[i];
      verts.canon[idx] = canonId;
      verts.count++;
      clustersByCanon[canonId] = {{(int32_t)idx, {fnUx, fnUy, fnUz}}};
      indices[i] = (uint32_t)idx;
    }
  }

  normalizeStoreNormals(verts);
}

// ── Indexed → non-indexed ────────────────────────────────────────────────────
void toNonIndexed(const VertStore& verts, const std::vector<uint32_t>& indices,
                  const std::vector<uint8_t>* faceExcluded, Geometry& outGeo,
                  std::vector<float>& outWeights) {
  const auto& positions = verts.pos;
  const auto& normals = verts.nrm;
  size_t triCount = indices.size() / 3;
  outGeo.positions.resize(triCount * 9);
  outGeo.normals.resize(triCount * 9);
  bool wantWgt = faceExcluded != nullptr || verts.hasWgt;
  if (wantWgt) outWeights.resize(triCount * 3);

  for (size_t t = 0; t < triCount; t++) {
    // Binary faceExcluded flag beats interpolated weights (MAX-merge can push
    // included faces past the 0.99 threshold).
    double faceW = -1;
    if (faceExcluded) faceW = (*faceExcluded)[t] ? 1.0 : 0.0;
    for (int v = 0; v < 3; v++) {
      uint32_t vidx = indices[t * 3 + v];
      outGeo.positions[t * 9 + v * 3] = (float)positions[vidx * 3];
      outGeo.positions[t * 9 + v * 3 + 1] = (float)positions[vidx * 3 + 1];
      outGeo.positions[t * 9 + v * 3 + 2] = (float)positions[vidx * 3 + 2];
      outGeo.normals[t * 9 + v * 3] = (float)normals[vidx * 3];
      outGeo.normals[t * 9 + v * 3 + 1] = (float)normals[vidx * 3 + 1];
      outGeo.normals[t * 9 + v * 3 + 2] = (float)normals[vidx * 3 + 2];
      if (wantWgt)
        outWeights[t * 3 + v] =
            faceW >= 0 ? (float)faceW : (float)(verts.hasWgt ? verts.wgt[vidx] : 0);
    }
  }
}

} // namespace

int64_t subdivisionSafetyCap() {
#if defined(_WIN32)
  MEMORYSTATUSEX ms;
  ms.dwLength = sizeof(ms);
  if (GlobalMemoryStatusEx(&ms)) {
    double gb = (double)ms.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
    if (gb >= 8.0) return kSubdivSafetyCapHigh;
  }
#endif
  return kSubdivSafetyCapLow;
}

SubdivideResult subdivide(const Geometry& geometry, double maxEdgeLength,
                          const SubdivideProgress& onProgress,
                          const std::vector<float>* faceWeights, bool fast,
                          int64_t safetyCap) {
  // Per-face exclusion BEFORE indexing (untouched non-indexed weights).
  std::vector<uint8_t> initialFaceExcluded;
  bool hasExcluded = false;
  if (faceWeights) {
    size_t triCount = faceWeights->size() / 3;
    initialFaceExcluded.assign(triCount, 0);
    for (size_t i = 0; i < triCount; i++) {
      if ((*faceWeights)[i * 3] > 0.99f) initialFaceExcluded[i] = 1;
    }
    hasExcluded = true;
  }

  VertStore verts;
  std::vector<uint32_t> indices;
  QuantizedPointMap posCanonMap(QUANTISE,
                                std::min(geometry.positions.size() / 3, (size_t)1 << 22));
  bool hasCanon = !fast;
  if (fast) {
    toIndexedFast(geometry, faceWeights, verts, indices);
  } else {
    toIndexed(geometry, faceWeights, verts, indices, posCanonMap);
  }

  const int maxIterations = 12;
  std::vector<uint32_t> currentIndices = std::move(indices);
  std::vector<uint8_t> currentFaceExcluded = std::move(initialFaceExcluded);
  bool safetyCapHit = false;

  size_t initialTriCount = currentIndices.size() / 3;
  std::vector<int32_t> currentFaceParentId(initialTriCount);
  for (size_t i = 0; i < initialTriCount; i++) currentFaceParentId[i] = (int32_t)i;

  for (int iter = 0; iter < maxIterations; iter++) {
    int64_t triCount = (int64_t)(currentIndices.size() / 3);
    if (triCount >= safetyCap) {
      safetyCapHit = true;
      break;
    }

    PassResult pr = subdividePass(
        verts, currentIndices, maxEdgeLength, safetyCap,
        hasExcluded ? &currentFaceExcluded : nullptr,
        hasCanon ? &posCanonMap : nullptr, &currentFaceParentId);
    currentIndices = std::move(pr.newIndices);
    if (!pr.newFaceExcluded.empty() || hasExcluded)
      currentFaceExcluded = std::move(pr.newFaceExcluded);
    if (!pr.newFaceParentId.empty())
      currentFaceParentId = std::move(pr.newFaceParentId);

    if (pr.capped || (int64_t)(currentIndices.size() / 3) >= safetyCap)
      safetyCapHit = true;

    // Report POST-pass state.
    const auto& positions = verts.pos;
    double maxEdgeLenSq = 0;
    for (size_t t = 0; t < currentIndices.size(); t += 3) {
      uint32_t a = currentIndices[t], b = currentIndices[t + 1], c = currentIndices[t + 2];
      double ab = edgeLenSq(positions, a, b);
      double bc = edgeLenSq(positions, b, c);
      double ca = edgeLenSq(positions, c, a);
      if (ab > maxEdgeLenSq) maxEdgeLenSq = ab;
      if (bc > maxEdgeLenSq) maxEdgeLenSq = bc;
      if (ca > maxEdgeLenSq) maxEdgeLenSq = ca;
    }
    double longestEdge = std::sqrt(maxEdgeLenSq);

    if (onProgress)
      onProgress(std::min(0.95, (double)(iter + 1) / maxIterations),
                 (int64_t)(currentIndices.size() / 3), longestEdge);
    if (!pr.changed || safetyCapHit) break;
  }

  SubdivideResult result;
  toNonIndexed(verts, currentIndices,
               hasExcluded ? &currentFaceExcluded : nullptr, result.geometry,
               result.excludeWeight);
  result.safetyCapHit = safetyCapHit;
  result.faceParentId = std::move(currentFaceParentId);
  return result;
}

} // namespace core
