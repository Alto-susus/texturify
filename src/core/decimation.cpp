#include "core/decimation.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/mesh_index.h"

namespace core {

namespace {

// 1e6 → 1 nm weld cells: exact-float welding for the pipeline's watertight
// copies while keeping genuinely distinct fine-feature vertices separate.
constexpr double QUANT_DEFAULT = 1e6;
constexpr double FLIP_DOT = 0.2; // cos ~78°
constexpr double FLIP_DOT_SQ = FLIP_DOT * FLIP_DOT;
constexpr double CREASE_COS = 0.5;    // cos 60°
constexpr double CREASE_WEIGHT = 1e4; // quadric penalty weight

// ── Struct-of-arrays min-heap (1-indexed, slot 0 = pop scratch) ─────────────
class SoAHeap {
public:
  explicit SoAHeap(size_t initialCap) {
    size_t cap = 2;
    while (cap <= initialCap) cap <<= 1;
    resize(cap);
  }

  size_t size() const { return _len; }

  void push(double cost, int32_t v1, int32_t v2, uint32_t ver1, uint32_t ver2,
            double px, double py, double pz) {
    size_t i = ++_len;
    if (i >= _cap) grow();
    _cost[i] = cost; _v1[i] = v1; _v2[i] = v2;
    _ver1[i] = ver1; _ver2[i] = ver2;
    _px[i] = px; _py[i] = py; _pz[i] = pz;
    bubbleUp(i);
  }

  // Pops the minimum entry into slot 0 and returns 0; -1 when empty.
  int pop() {
    if (_len == 0) return -1;
    copySlot(0, 1);
    copySlot(1, _len--);
    if (_len > 0) sinkDown(1);
    return 0;
  }

  double cost(size_t i) const { return _cost[i]; }
  int32_t v1(size_t i) const { return _v1[i]; }
  int32_t v2(size_t i) const { return _v2[i]; }
  uint32_t ver1(size_t i) const { return _ver1[i]; }
  uint32_t ver2(size_t i) const { return _ver2[i]; }
  double px(size_t i) const { return _px[i]; }
  double py(size_t i) const { return _py[i]; }
  double pz(size_t i) const { return _pz[i]; }

private:
  size_t _cap = 0, _len = 0;
  std::vector<double> _cost, _px, _py, _pz;
  std::vector<int32_t> _v1, _v2;
  std::vector<uint32_t> _ver1, _ver2;

  void resize(size_t cap) {
    _cap = cap;
    _cost.resize(cap); _px.resize(cap); _py.resize(cap); _pz.resize(cap);
    _v1.resize(cap); _v2.resize(cap);
    _ver1.resize(cap); _ver2.resize(cap);
  }
  void grow() { resize((size_t)std::ceil(_cap * 1.5) + 2); }

  void copySlot(size_t dst, size_t src) {
    _cost[dst] = _cost[src]; _v1[dst] = _v1[src]; _v2[dst] = _v2[src];
    _ver1[dst] = _ver1[src]; _ver2[dst] = _ver2[src];
    _px[dst] = _px[src]; _py[dst] = _py[src]; _pz[dst] = _pz[src];
  }

  void bubbleUp(size_t idx) {
    double cost = _cost[idx];
    int32_t v1 = _v1[idx], v2 = _v2[idx];
    uint32_t ver1 = _ver1[idx], ver2 = _ver2[idx];
    double px = _px[idx], py = _py[idx], pz = _pz[idx];
    while (idx > 1) {
      size_t parent = idx >> 1;
      if (_cost[parent] <= cost) break;
      copySlot(idx, parent);
      idx = parent;
    }
    _cost[idx] = cost;
    _v1[idx] = v1; _v2[idx] = v2;
    _ver1[idx] = ver1; _ver2[idx] = ver2;
    _px[idx] = px; _py[idx] = py; _pz[idx] = pz;
  }

  void sinkDown(size_t idx) {
    size_t n = _len;
    double cost = _cost[idx];
    int32_t v1 = _v1[idx], v2 = _v2[idx];
    uint32_t ver1 = _ver1[idx], ver2 = _ver2[idx];
    double px = _px[idx], py = _py[idx], pz = _pz[idx];
    for (;;) {
      size_t l = idx << 1, r = l | 1;
      size_t child = 0;
      bool has = false;
      if (l <= n && _cost[l] < cost) { child = l; has = true; }
      if (r <= n && _cost[r] < (has ? _cost[child] : cost)) { child = r; has = true; }
      if (!has) break;
      copySlot(idx, child);
      idx = child;
    }
    _cost[idx] = cost;
    _v1[idx] = v1; _v2[idx] = v2;
    _ver1[idx] = ver1; _ver2[idx] = ver2;
    _px[idx] = px; _py[idx] = py; _pz[idx] = pz;
  }
};

// ── Quadric helpers: symmetric 4×4 as 10 upper-triangle doubles ─────────────
inline void addPlaneQ(std::vector<double>& q, int32_t v, double a, double b,
                      double c, double d) {
  size_t o = (size_t)v * 10;
  q[o] += a * a; q[o + 1] += a * b; q[o + 2] += a * c; q[o + 3] += a * d;
  q[o + 4] += b * b; q[o + 5] += b * c; q[o + 6] += b * d;
  q[o + 7] += c * c; q[o + 8] += c * d;
  q[o + 9] += d * d;
}

inline void mergeQuadric(std::vector<double>& q, int32_t v1, int32_t v2) {
  size_t o1 = (size_t)v1 * 10, o2 = (size_t)v2 * 10;
  for (int i = 0; i < 10; i++) q[o1 + i] += q[o2 + i];
}

inline double evalQ(const std::vector<double>& q, int32_t v, double x, double y,
                    double z) {
  size_t o = (size_t)v * 10;
  return q[o] * x * x + 2 * q[o + 1] * x * y + 2 * q[o + 2] * x * z + 2 * q[o + 3] * x +
         q[o + 4] * y * y + 2 * q[o + 5] * y * z + 2 * q[o + 6] * y +
         q[o + 7] * z * z + 2 * q[o + 8] * z + q[o + 9];
}

inline double evalQSum(const std::vector<double>& q, int32_t v1, int32_t v2,
                       double x, double y, double z) {
  return evalQ(q, v1, x, y, z) + evalQ(q, v2, x, y, z);
}

bool solveQ(const std::vector<double>& q, int32_t v1, int32_t v2, double s[3]) {
  size_t o1 = (size_t)v1 * 10, o2 = (size_t)v2 * 10;
  double a00 = q[o1] + q[o2];
  double a01 = q[o1 + 1] + q[o2 + 1];
  double a02 = q[o1 + 2] + q[o2 + 2];
  double a11 = q[o1 + 4] + q[o2 + 4];
  double a12 = q[o1 + 5] + q[o2 + 5];
  double a22 = q[o1 + 7] + q[o2 + 7];
  double b0 = -(q[o1 + 3] + q[o2 + 3]);
  double b1 = -(q[o1 + 6] + q[o2 + 6]);
  double b2 = -(q[o1 + 8] + q[o2 + 8]);

  double det = a00 * (a11 * a22 - a12 * a12) - a01 * (a01 * a22 - a12 * a02) +
               a02 * (a01 * a12 - a11 * a02);
  double maxEl = std::max({std::abs(a00), std::abs(a01), std::abs(a02),
                           std::abs(a11), std::abs(a12), std::abs(a22)});
  double threshold = maxEl * maxEl * maxEl * 1e-10;
  if (std::abs(det) < std::max(threshold, 1e-30)) return false;

  double inv = 1 / det;
  s[0] = inv * (b0 * (a11 * a22 - a12 * a12) - a01 * (b1 * a22 - a12 * b2) +
                a02 * (b1 * a12 - a11 * b2));
  s[1] = inv * (a00 * (b1 * a22 - a12 * b2) - b0 * (a01 * a22 - a12 * a02) +
                a02 * (a01 * b2 - b1 * a02));
  s[2] = inv * (a00 * (a11 * b2 - b1 * a12) - a01 * (a01 * b2 - b1 * a02) +
                b0 * (a01 * a12 - a11 * a02));
  return true;
}

void pushEdge(SoAHeap& heap, const std::vector<double>& quadrics,
              const std::vector<double>& positions,
              const std::vector<uint32_t>& version, int32_t v1, int32_t v2) {
  double px, py, pz;
  double s[3];
  if (solveQ(quadrics, v1, v2, s)) {
    px = s[0]; py = s[1]; pz = s[2];
  } else {
    double mx = (positions[v1 * 3] + positions[v2 * 3]) / 2;
    double my = (positions[v1 * 3 + 1] + positions[v2 * 3 + 1]) / 2;
    double mz = (positions[v1 * 3 + 2] + positions[v2 * 3 + 2]) / 2;
    double e1 = evalQSum(quadrics, v1, v2, positions[v1 * 3], positions[v1 * 3 + 1],
                         positions[v1 * 3 + 2]);
    double e2 = evalQSum(quadrics, v1, v2, positions[v2 * 3], positions[v2 * 3 + 1],
                         positions[v2 * 3 + 2]);
    double em = evalQSum(quadrics, v1, v2, mx, my, mz);
    // Prefer midpoint when costs are near-equal (flat surfaces) — minimises
    // displacement of adjacent triangles, fewer normal flips.
    double eMin = std::min({e1, e2, em});
    double eTol = eMin * 1e-2 + 1e-12;
    if (em <= eMin + eTol) { px = mx; py = my; pz = mz; }
    else if (e1 <= e2) { px = positions[v1 * 3]; py = positions[v1 * 3 + 1]; pz = positions[v1 * 3 + 2]; }
    else { px = positions[v2 * 3]; py = positions[v2 * 3 + 1]; pz = positions[v2 * 3 + 2]; }
  }

  double cost = evalQSum(quadrics, v1, v2, px, py, pz);
  // Tiny edge-length tiebreaker for flat regions.
  double dx = positions[v2 * 3] - positions[v1 * 3];
  double dy = positions[v2 * 3 + 1] - positions[v1 * 3 + 1];
  double dz = positions[v2 * 3 + 2] - positions[v1 * 3 + 2];
  heap.push(cost + (dx * dx + dy * dy + dz * dz) * 1e-8, v1, v2, version[v1],
            version[v2], px, py, pz);
}

void initQuadrics(std::vector<double>& quadrics, const std::vector<double>& positions,
                  const std::vector<int32_t>& faces, size_t faceCount) {
  for (size_t f = 0; f < faceCount; f++) {
    if (faces[f * 3] < 0) continue;
    int32_t fa = faces[f * 3], fb = faces[f * 3 + 1], fc = faces[f * 3 + 2];
    double ux = positions[fb * 3] - positions[fa * 3];
    double uy = positions[fb * 3 + 1] - positions[fa * 3 + 1];
    double uz = positions[fb * 3 + 2] - positions[fa * 3 + 2];
    double vx = positions[fc * 3] - positions[fa * 3];
    double vy = positions[fc * 3 + 1] - positions[fa * 3 + 1];
    double vz = positions[fc * 3 + 2] - positions[fa * 3 + 2];
    double cnx = uy * vz - uz * vy, cny = uz * vx - ux * vz, cnz = ux * vy - uy * vx;
    double len = std::sqrt(cnx * cnx + cny * cny + cnz * cnz);
    if (len == 0) len = 1;
    double nx = cnx / len, ny = cny / len, nz = cnz / len;
    double d = -(nx * positions[fa * 3] + ny * positions[fa * 3 + 1] +
                 nz * positions[fa * 3 + 2]);
    addPlaneQ(quadrics, fa, nx, ny, nz, d);
    addPlaneQ(quadrics, fb, nx, ny, nz, d);
    addPlaneQ(quadrics, fc, nx, ny, nz, d);
  }
}

// Crease preservation: penalty planes on sharp interior edges, accumulated in
// FIRST-OCCURRENCE edge order (float adds — order shifts low bits, so it must
// match the JS exactly).
void addCreaseQuadrics(std::vector<double>& quadrics,
                       const std::vector<double>& positions,
                       const std::vector<int32_t>& faces, size_t faceCount) {
  size_t maxEdges = faceCount * 3;
  QuantizedPointMap edgeIdx(1, std::min(maxEdges, (size_t)1 << 22));
  std::vector<int32_t> edgeVa(maxEdges), edgeVb(maxEdges), edgeF0(maxEdges),
      edgeF1(maxEdges);
  std::vector<uint8_t> edgeNum(maxEdges, 0); // 1, 2, or 3 (= ">2, skip")
  int32_t edgeCount = 0;
  for (size_t f = 0; f < faceCount; f++) {
    if (faces[f * 3] < 0) continue;
    for (int e = 0; e < 3; e++) {
      int32_t va = faces[f * 3 + e];
      int32_t vb = faces[f * 3 + ((e + 1) % 3)];
      int32_t lo = va < vb ? va : vb, hi = va < vb ? vb : va;
      int32_t ei = edgeIdx.getOrSet(lo, hi, 0, edgeCount);
      if (edgeIdx.inserted) {
        edgeVa[ei] = lo; edgeVb[ei] = hi; edgeF0[ei] = (int32_t)f; edgeNum[ei] = 1;
        edgeCount++;
      } else if (edgeNum[ei] == 1) {
        edgeF1[ei] = (int32_t)f;
        edgeNum[ei] = 2;
      } else {
        // 3rd+ incidence: non-manifold edge — never feeds crease quadrics.
        edgeNum[ei] = 3;
      }
    }
  }

  double sqrtW = std::sqrt(CREASE_WEIGHT);

  for (int32_t ei = 0; ei < edgeCount; ei++) {
    if (edgeNum[ei] != 2) continue;
    int32_t f0 = edgeF0[ei], f1 = edgeF1[ei];
    int32_t v0a = faces[f0 * 3], v0b = faces[f0 * 3 + 1], v0c = faces[f0 * 3 + 2];
    int32_t v1a = faces[f1 * 3], v1b = faces[f1 * 3 + 1], v1c = faces[f1 * 3 + 2];

    double ux = positions[v0b * 3] - positions[v0a * 3];
    double uy = positions[v0b * 3 + 1] - positions[v0a * 3 + 1];
    double uz = positions[v0b * 3 + 2] - positions[v0a * 3 + 2];
    double vx = positions[v0c * 3] - positions[v0a * 3];
    double vy = positions[v0c * 3 + 1] - positions[v0a * 3 + 1];
    double vz = positions[v0c * 3 + 2] - positions[v0a * 3 + 2];
    double cnx = uy * vz - uz * vy, cny = uz * vx - ux * vz, cnz = ux * vy - uy * vx;
    double clen = std::sqrt(cnx * cnx + cny * cny + cnz * cnz);
    if (clen == 0) clen = 1;
    double n0x = cnx / clen, n0y = cny / clen, n0z = cnz / clen;

    ux = positions[v1b * 3] - positions[v1a * 3];
    uy = positions[v1b * 3 + 1] - positions[v1a * 3 + 1];
    uz = positions[v1b * 3 + 2] - positions[v1a * 3 + 2];
    vx = positions[v1c * 3] - positions[v1a * 3];
    vy = positions[v1c * 3 + 1] - positions[v1a * 3 + 1];
    vz = positions[v1c * 3 + 2] - positions[v1a * 3 + 2];
    cnx = uy * vz - uz * vy; cny = uz * vx - ux * vz; cnz = ux * vy - uy * vx;
    clen = std::sqrt(cnx * cnx + cny * cny + cnz * cnz);
    if (clen == 0) clen = 1;
    double n1x = cnx / clen, n1y = cny / clen, n1z = cnz / clen;

    if (n0x * n1x + n0y * n1y + n0z * n1z >= CREASE_COS) continue; // smooth

    int32_t va = edgeVa[ei], vb = edgeVb[ei];

    double ex = positions[vb * 3] - positions[va * 3];
    double ey = positions[vb * 3 + 1] - positions[va * 3 + 1];
    double ez = positions[vb * 3 + 2] - positions[va * 3 + 2];
    double elen = std::sqrt(ex * ex + ey * ey + ez * ez);
    if (elen == 0) elen = 1;
    double edx = ex / elen, edy = ey / elen, edz = ez / elen;

    // One penalty plane per adjacent face normal (n0 first, then n1).
    for (int pi = 0; pi < 2; pi++) {
      double nx = pi == 0 ? n0x : n1x, ny = pi == 0 ? n0y : n1y, nz = pi == 0 ? n0z : n1z;
      // Penalty plane normal = normalize(face_normal × edge_dir): contains the
      // edge, perpendicular to the face → constrains vertex to the crease line.
      double px = ny * edz - nz * edy;
      double py = nz * edx - nx * edz;
      double pz = nx * edy - ny * edx;
      double plen = std::sqrt(px * px + py * py + pz * pz);
      if (plen < 1e-10) continue; // edge parallel to face normal — degenerate
      px /= plen; py /= plen; pz /= plen;
      double d = -(px * positions[va * 3] + py * positions[va * 3 + 1] +
                   pz * positions[va * 3 + 2]);
      addPlaneQ(quadrics, va, px * sqrtW, py * sqrtW, pz * sqrtW, d * sqrtW);
      addPlaneQ(quadrics, vb, px * sqrtW, py * sqrtW, pz * sqrtW, d * sqrtW);
    }
  }
}

// ── Linked-list vertex-face incidence ────────────────────────────────────────
struct LinkedAdj {
  std::vector<int32_t> vfHead, slotFace, slotVert, slotNext, slotPrev, faceSlot;
};

LinkedAdj buildLinkedAdj(const std::vector<int32_t>& faces, size_t faceCount,
                         size_t vertCount) {
  size_t S = faceCount * 3;
  LinkedAdj a;
  a.vfHead.assign(vertCount, -1);
  a.slotFace.assign(S, 0);
  a.slotVert.assign(S, 0);
  a.slotNext.assign(S, -1);
  a.slotPrev.assign(S, -1);
  a.faceSlot.assign(S, -1);
  for (size_t f = 0; f < faceCount; f++) {
    if (faces[f * 3] < 0) continue;
    for (int k = 0; k < 3; k++) {
      int32_t v = faces[f * 3 + k];
      int32_t s = (int32_t)(f * 3 + k);
      a.slotFace[s] = (int32_t)f;
      a.slotVert[s] = v;
      int32_t h = a.vfHead[v];
      a.slotNext[s] = h;
      a.slotPrev[s] = -1;
      if (h >= 0) a.slotPrev[h] = s;
      a.vfHead[v] = s;
      a.faceSlot[f * 3 + k] = s;
    }
  }
  return a;
}

inline void unlinkSlot(int32_t s, LinkedAdj& a) {
  int32_t v = a.slotVert[s], p = a.slotPrev[s], n = a.slotNext[s];
  if (p < 0) a.vfHead[v] = n;
  else a.slotNext[p] = n;
  if (n >= 0) a.slotPrev[n] = p;
}

inline void moveSlot(int32_t s, int32_t nv, LinkedAdj& a) {
  unlinkSlot(s, a);
  int32_t h = a.vfHead[nv];
  a.slotNext[s] = h;
  a.slotPrev[s] = -1;
  if (h >= 0) a.slotPrev[h] = s;
  a.vfHead[nv] = s;
  a.slotVert[s] = nv;
}

// Guard 0+1: 0 = stale, 1 = boundary edge, ≥2 = safe.
int sharedFaceCount(const std::vector<int32_t>& faces, const LinkedAdj& a,
                    int32_t v1, int32_t v2) {
  int count = 0;
  for (int32_t s = a.vfHead[v1]; s >= 0; s = a.slotNext[s]) {
    int32_t f = a.slotFace[s];
    if (faces[f * 3] < 0) continue;
    int32_t fa = faces[f * 3], fb = faces[f * 3 + 1], fc = faces[f * 3 + 2];
    if (fa == v2 || fb == v2 || fc == v2) {
      if (++count >= 2) return 2;
    }
  }
  return count;
}

// Guard 2: complete link condition. lkStamp[w]==ep → one-ring neighbour of
// v1; ==ep+1 → legal shared-face apex.
bool hasLinkViolation(const std::vector<int32_t>& faces, const LinkedAdj& a,
                      int32_t v1, int32_t v2, std::vector<uint32_t>& lkStamp,
                      uint32_t ep) {
  for (int32_t s = a.vfHead[v1]; s >= 0; s = a.slotNext[s]) {
    int32_t f = a.slotFace[s];
    if (faces[f * 3] < 0) continue;
    int32_t fa = faces[f * 3], fb = faces[f * 3 + 1], fc = faces[f * 3 + 2];
    if (fa != v1) lkStamp[fa] = ep;
    if (fb != v1) lkStamp[fb] = ep;
    if (fc != v1) lkStamp[fc] = ep;
  }
  int shared = 0;
  for (int32_t s = a.vfHead[v1]; s >= 0; s = a.slotNext[s]) {
    int32_t f = a.slotFace[s];
    if (faces[f * 3] < 0) continue;
    int32_t fa = faces[f * 3], fb = faces[f * 3 + 1], fc = faces[f * 3 + 2];
    if (fa == v2 || fb == v2 || fc == v2) {
      shared++;
      int32_t apex = (fa != v1 && fa != v2) ? fa : (fb != v1 && fb != v2) ? fb : fc;
      lkStamp[apex] = ep + 1;
    }
  }
  if (shared > 2) return true; // already non-manifold (3+ shared faces)
  for (int32_t s = a.vfHead[v2]; s >= 0; s = a.slotNext[s]) {
    int32_t f = a.slotFace[s];
    if (faces[f * 3] < 0) continue;
    int32_t fa = faces[f * 3], fb = faces[f * 3 + 1], fc = faces[f * 3 + 2];
    if (fa != v2 && fa != v1 && lkStamp[fa] == ep) return true;
    if (fb != v2 && fb != v1 && lkStamp[fb] == ep) return true;
    if (fc != v2 && fc != v1 && lkStamp[fc] == ep) return true;
  }
  return false;
}

// Guard 3: normal-flip rejection (squared-dot comparison, no sqrt).
bool checkFlipped(const std::vector<double>& positions, const LinkedAdj& a,
                  const std::vector<int32_t>& faces, int32_t vc, int32_t vo,
                  double npx, double npy, double npz) {
  for (int32_t s = a.vfHead[vc]; s >= 0; s = a.slotNext[s]) {
    int32_t f = a.slotFace[s];
    if (faces[f * 3] < 0) continue;
    int32_t fa = faces[f * 3], fb = faces[f * 3 + 1], fc = faces[f * 3 + 2];
    if (fa == vo || fb == vo || fc == vo) continue;
    double oax = positions[fa * 3], oay = positions[fa * 3 + 1], oaz = positions[fa * 3 + 2];
    double obx = positions[fb * 3], oby = positions[fb * 3 + 1], obz = positions[fb * 3 + 2];
    double ocx = positions[fc * 3], ocy = positions[fc * 3 + 1], ocz = positions[fc * 3 + 2];
    double oux = obx - oax, ouy = oby - oay, ouz = obz - oaz;
    double ovx = ocx - oax, ovy = ocy - oay, ovz = ocz - oaz;
    double onx = ouy * ovz - ouz * ovy;
    double ony = ouz * ovx - oux * ovz;
    double onz = oux * ovy - ouy * ovx;
    double nax, nay, naz, nbx, nby, nbz, ncx, ncy, ncz;
    if (fa == vc) { nax = npx; nay = npy; naz = npz; nbx = obx; nby = oby; nbz = obz; ncx = ocx; ncy = ocy; ncz = ocz; }
    else if (fb == vc) { nax = oax; nay = oay; naz = oaz; nbx = npx; nby = npy; nbz = npz; ncx = ocx; ncy = ocy; ncz = ocz; }
    else { nax = oax; nay = oay; naz = oaz; nbx = obx; nby = oby; nbz = obz; ncx = npx; ncy = npy; ncz = npz; }
    double nux = nbx - nax, nuy = nby - nay, nuz = nbz - naz;
    double nvx = ncx - nax, nvy = ncy - nay, nvz = ncz - naz;
    double nnx = nuy * nvz - nuz * nvy;
    double nny = nuz * nvx - nux * nvz;
    double nnz = nux * nvy - nuy * nvx;
    double rawDot = onx * nnx + ony * nny + onz * nnz;
    if (rawDot < 0) return true;
    if (rawDot * rawDot < FLIP_DOT_SQ * (onx * onx + ony * ony + onz * onz) *
                              (nnx * nnx + nny * nny + nnz * nnz))
      return true;
  }
  return false;
}

struct IndexedMesh {
  std::vector<double> positions;
  std::vector<int32_t> faces;
  size_t vertCount = 0;
  size_t faceCount = 0;
};

IndexedMesh buildIndexed(const Geometry& geometry) {
  const auto& posArr = geometry.positions;
  size_t n = posArr.size() / 3;

  IndexedMesh m;
  m.positions.resize(n * 3);
  std::vector<int32_t> indexRemap(n);
  int32_t vertCount = 0;

  QuantizedPointMap vertMap(QUANT_DEFAULT, std::min(n, (size_t)1 << 22));

  for (size_t i = 0; i < n; i++) {
    double x = posArr[i * 3], y = posArr[i * 3 + 1], z = posArr[i * 3 + 2];
    int32_t idx = vertMap.getOrSet(x, y, z, vertCount);
    if (vertMap.inserted) {
      vertCount++;
      m.positions[idx * 3] = x;
      m.positions[idx * 3 + 1] = y;
      m.positions[idx * 3 + 2] = z;
    }
    indexRemap[i] = idx;
  }

  m.faceCount = n / 3;
  m.faces.resize(n);
  for (size_t i = 0; i < n; i++) m.faces[i] = indexRemap[i];
  m.vertCount = (size_t)vertCount;
  m.positions.resize((size_t)vertCount * 3);
  return m;
}

Geometry buildOutput(const std::vector<double>& positions,
                     const std::vector<int32_t>& faces, size_t faceCount) {
  size_t activeFaces = 0;
  for (size_t f = 0; f < faceCount; f++)
    if (faces[f * 3] >= 0) activeFaces++;

  Geometry geo;
  geo.positions.resize(activeFaces * 9);
  size_t out = 0;
  for (size_t f = 0; f < faceCount; f++) {
    if (faces[f * 3] < 0) continue;
    for (int v = 0; v < 3; v++) {
      int32_t vi = faces[f * 3 + v];
      geo.positions[out++] = (float)positions[vi * 3];
      geo.positions[out++] = (float)positions[vi * 3 + 1];
      geo.positions[out++] = (float)positions[vi * 3 + 2];
    }
  }

  // Exact per-face normals from the final float32 positions.
  geo.normals.resize(geo.positions.size());
  const auto& p = geo.positions;
  for (size_t i = 0; i + 8 < p.size(); i += 9) {
    double ax = p[i], ay = p[i + 1], az = p[i + 2];
    double bx = p[i + 3], by = p[i + 4], bz = p[i + 5];
    double cx = p[i + 6], cy = p[i + 7], cz = p[i + 8];
    double ux = bx - ax, uy = by - ay, uz = bz - az;
    double vx = cx - ax, vy = cy - ay, vz = cz - az;
    double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len == 0) len = 1;
    float fx = (float)(nx / len), fy = (float)(ny / len), fz = (float)(nz / len);
    geo.normals[i] = geo.normals[i + 3] = geo.normals[i + 6] = fx;
    geo.normals[i + 1] = geo.normals[i + 4] = geo.normals[i + 7] = fy;
    geo.normals[i + 2] = geo.normals[i + 5] = geo.normals[i + 8] = fz;
  }
  return geo;
}

} // namespace

Geometry decimate(const Geometry& geometry, int64_t targetTriangles,
                  const std::function<void(double)>& onProgress, bool harvestFlat,
                  double harvestTol) {
  IndexedMesh mesh = buildIndexed(geometry);
  auto& positions = mesh.positions;
  auto& faces = mesh.faces;
  const size_t vertCount = mesh.vertCount;
  const size_t faceCount = mesh.faceCount;

  if ((int64_t)faceCount <= targetTriangles && !harvestFlat)
    return buildOutput(positions, faces, faceCount);

  std::vector<double> quadrics(vertCount * 10, 0.0);
  initQuadrics(quadrics, positions, faces, faceCount);
  addCreaseQuadrics(quadrics, positions, faces, faceCount);

  LinkedAdj adj = buildLinkedAdj(faces, faceCount, vertCount);

  std::vector<uint8_t> active(vertCount, 1);
  std::vector<uint32_t> version(vertCount, 0);
  std::vector<uint32_t> nbStamp(vertCount, 0);
  uint32_t epoch = 1;
  std::vector<uint32_t> lkStamp(vertCount, 0);
  uint32_t lkEpoch = 1;
  int64_t activeFaces = (int64_t)faceCount;

  SoAHeap heap(std::min(faceCount * 3, (size_t)1 << 24));
  QuantizedPointMap seedSeen(1, std::min(faceCount * 3, (size_t)1 << 22));
  for (size_t f = 0; f < faceCount; f++) {
    if (faces[f * 3] < 0) continue;
    for (int e = 0; e < 3; e++) {
      int32_t va = faces[f * 3 + e];
      int32_t vb = faces[f * 3 + ((e + 1) % 3)];
      int32_t lo = va < vb ? va : vb, hi = va < vb ? vb : va;
      seedSeen.getOrSet(lo, hi, 0, 1);
      if (seedSeen.inserted) pushEdge(heap, quadrics, positions, version, va, vb);
    }
  }

  const int64_t initFaces = activeFaces;
  const int64_t toRemove = std::max((int64_t)1, initFaces - targetTriangles);
  double lastProg = 0;
  int64_t iterations = 0;

  const double harvestCeil = harvestTol * harvestTol;
  bool reachedTarget = false;

  while (heap.size() > 0) {
    if (activeFaces <= targetTriangles) {
      if (!harvestFlat) break;
      reachedTarget = true;
    }

    int idx = heap.pop();
    if (idx < 0) break;
    double cost = heap.cost(idx);

    if (reachedTarget && cost > harvestCeil) break;

    ++iterations;
    if (onProgress && (iterations & 511) == 0) {
      double p = std::min(1.0, (double)(initFaces - activeFaces) / toRemove);
      if (p - lastProg > 0.005) {
        onProgress(p);
        lastProg = p;
      }
    }

    int32_t v1 = heap.v1(idx), v2 = heap.v2(idx);
    uint32_t ver1 = heap.ver1(idx), ver2 = heap.ver2(idx);
    double px = heap.px(idx), py = heap.py(idx), pz = heap.pz(idx);

    // Stale-entry checks (lazy deletion)
    if (!active[v1] || !active[v2]) continue;
    if (version[v1] != ver1 || version[v2] != ver2) continue;

    int nsh = sharedFaceCount(faces, adj, v1, v2);
    if (nsh < 2) continue;

    // ── Three safety guards ─────────────────────────────────────────────────
    lkEpoch += 2;
    if (hasLinkViolation(faces, adj, v1, v2, lkStamp, lkEpoch)) continue;
    if (checkFlipped(positions, adj, faces, v1, v2, px, py, pz)) continue;
    if (checkFlipped(positions, adj, faces, v2, v1, px, py, pz)) continue;

    // ── Collapse: keep v1 at new position, remove v2 ────────────────────────
    positions[v1 * 3] = px;
    positions[v1 * 3 + 1] = py;
    positions[v1 * 3 + 2] = pz;
    mergeQuadric(quadrics, v1, v2);
    version[v1]++;

    int32_t s = adj.vfHead[v2];
    while (s >= 0) {
      int32_t f = adj.slotFace[s];
      int32_t sNext = adj.slotNext[s]; // read BEFORE list modification
      if (faces[f * 3] >= 0) {
        int cv2 = faces[f * 3] == v2 ? 0 : faces[f * 3 + 1] == v2 ? 1 : 2;
        faces[f * 3 + cv2] = v1;
        int32_t fa = faces[f * 3], fb = faces[f * 3 + 1], fc = faces[f * 3 + 2];
        if (fa == fb || fb == fc || fa == fc) {
          // Degenerate: unlink all 3 slots
          for (int k = 0; k < 3; k++) {
            int32_t sk = adj.faceSlot[f * 3 + k];
            if (sk >= 0) {
              unlinkSlot(sk, adj);
              adj.faceSlot[f * 3 + k] = -1;
            }
          }
          faces[f * 3] = faces[f * 3 + 1] = faces[f * 3 + 2] = -1;
          activeFaces--;
        } else {
          moveSlot(s, v1, adj);
        }
      }
      s = sNext;
    }
    active[v2] = 0;

    // Re-push edges for v1's updated neighbourhood
    epoch++;
    for (int32_t sv = adj.vfHead[v1]; sv >= 0; sv = adj.slotNext[sv]) {
      int32_t f = adj.slotFace[sv];
      if (faces[f * 3] < 0) continue;
      for (int k = 0; k < 3; k++) {
        int32_t nb = faces[f * 3 + k];
        if (nb != v1 && nbStamp[nb] != epoch) {
          nbStamp[nb] = epoch;
          if (active[nb]) pushEdge(heap, quadrics, positions, version, v1, nb);
        }
      }
    }
  }

  if (onProgress) onProgress(1);
  return buildOutput(positions, faces, faceCount);
}

} // namespace core
