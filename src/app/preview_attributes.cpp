#include "app/preview_attributes.h"

#include <cmath>

#include "core/mesh_index.h"

namespace app {

std::vector<float> computeFaceNormals(const core::Geometry& geo) {
  const std::vector<float>& pos = geo.positions;
  const size_t count = pos.size() / 3;
  std::vector<float> fn(count * 3);
  for (size_t i = 0; i + 2 < count; i += 3) {
    const double ax = pos[i * 3], ay = pos[i * 3 + 1], az = pos[i * 3 + 2];
    const double e1x = pos[(i + 1) * 3] - ax, e1y = pos[(i + 1) * 3 + 1] - ay,
                 e1z = pos[(i + 1) * 3 + 2] - az;
    const double e2x = pos[(i + 2) * 3] - ax, e2y = pos[(i + 2) * 3 + 1] - ay,
                 e2z = pos[(i + 2) * 3 + 2] - az;
    double nx = e1y * e2z - e1z * e2y;
    double ny = e1z * e2x - e1x * e2z;
    double nz = e1x * e2y - e1y * e2x;
    double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len == 0) len = 1; // three.js normalize(): divideScalar(length || 1)
    nx /= len; ny /= len; nz /= len;
    for (int v = 0; v < 3; v++) {
      fn[(i + v) * 3] = (float)nx;
      fn[(i + v) * 3 + 1] = (float)ny;
      fn[(i + v) * 3 + 2] = (float)nz;
    }
  }
  return fn;
}

std::vector<float> computeSmoothNormals(const core::Geometry& geo) {
  const std::vector<float>& pos = geo.positions;
  const size_t count = pos.size() / 3;
  const std::vector<float>& nrm = geo.normals;

  // Vertex-dedup pass (QUANT 1e4, first occurrence wins)
  core::WeldResult weld = core::weldVertices(pos.data(), count, 1e4);

  const size_t uc = weld.uniqueCount;
  std::vector<double> snx(uc, 0), sny(uc, 0), snz(uc, 0);
  for (size_t i = 0; i + 2 < count; i += 3) {
    const double ax = pos[i * 3], ay = pos[i * 3 + 1], az = pos[i * 3 + 2];
    const double e1x = pos[(i + 1) * 3] - ax, e1y = pos[(i + 1) * 3 + 1] - ay,
                 e1z = pos[(i + 1) * 3 + 2] - az;
    const double e2x = pos[(i + 2) * 3] - ax, e2y = pos[(i + 2) * 3 + 1] - ay,
                 e2z = pos[(i + 2) * 3 + 2] - az;
    const double fx = e1y * e2z - e1z * e2y;
    const double fy = e1z * e2x - e1x * e2z;
    const double fz = e1x * e2y - e1y * e2x;
    const double area = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (area < 1e-12) continue;
    for (int v = 0; v < 3; v++) {
      const size_t vi = i + v;
      const uint32_t id = weld.vertexId[vi];
      snx[id] += (double)nrm[vi * 3] * area;
      sny[id] += (double)nrm[vi * 3 + 1] * area;
      snz[id] += (double)nrm[vi * 3 + 2] * area;
    }
  }

  for (size_t id = 0; id < uc; id++) {
    double len =
        std::sqrt(snx[id] * snx[id] + sny[id] * sny[id] + snz[id] * snz[id]);
    if (len == 0) len = 1; // JS: || 1
    snx[id] /= len; sny[id] /= len; snz[id] /= len;
  }

  std::vector<float> sn(count * 3);
  for (size_t i = 0; i < count; i++) {
    const uint32_t id = weld.vertexId[i];
    sn[i * 3] = (float)snx[id];
    sn[i * 3 + 1] = (float)sny[id];
    sn[i * 3 + 2] = (float)snz[id];
  }
  return sn;
}

PreviewAttributeData buildDefaultPreviewAttributes(const core::Geometry& geo) {
  const size_t count = geo.positions.size() / 3;
  PreviewAttributeData d;
  d.faceNormal = computeFaceNormals(geo);
  d.smoothNormal = geo.normals.size() == geo.positions.size()
                       ? computeSmoothNormals(geo)
                       : d.faceNormal;
  d.faceMask.assign(count, 1.0f);
  d.boundaryFalloff.assign(count, 1.0f);
  d.boundaryMaskType.assign(count, 1.0f);
  return d;
}

} // namespace app
