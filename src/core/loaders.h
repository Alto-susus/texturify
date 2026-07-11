// Port of reference/js/stlLoader.js — model file loading.
//
// STL parsing mirrors three.js STLLoader (MIT), OBJ parsing covers the
// geometry subset of three.js OBJLoader the app uses, and the 3MF parser is
// a direct port of the custom parser in stlLoader.js (multi-file production
// 3MFs, component transforms, unit scaling, 10M-triangle cap).
//
// After parsing, geometry is validated (NaN / degenerate triangles removed),
// centered on the origin, and normals are computed when absent — identical to
// setupGeometry() in the original.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/geometry.h"

namespace core {

struct LoadResult {
  bool ok = false;
  std::string error;
  Geometry geometry;
  Bounds bounds;
  int64_t nanCount = 0;
  int64_t degenerateCount = 0;
  Vec3 originOffset; // translation subtracted to centre the mesh
};

inline constexpr int64_t kMaxFileSize = 500ll * 1024 * 1024; // 500 MB
inline constexpr int64_t kMax3MFTriangles = 10'000'000;
inline constexpr int kMax3MFDepth = 32;

// Parse from raw bytes. `loadModelFile` dispatches on the (lowercased) file
// extension exactly like the original: "obj" → OBJ, "3mf" → 3MF, else STL.
LoadResult loadSTL(const uint8_t* data, size_t size);
LoadResult loadOBJ(const uint8_t* data, size_t size);
LoadResult load3MF(const uint8_t* data, size_t size);
LoadResult loadModelBytes(const std::string& filename, const uint8_t* data, size_t size);
LoadResult loadModelFile(const std::string& path);

// Helpers shared with the rest of the pipeline (ports of the JS exports).
size_t getTriangleCount(const Geometry& g);
double computeSurfaceArea(const Geometry& g);

// three.js BufferGeometry.computeVertexNormals for non-indexed geometry:
// unnormalized face normal (C-B)x(A-B) written per-vertex, then normalized.
void computeVertexNormals(Geometry& g);

} // namespace core
