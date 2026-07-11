#include "app/cylinder_silhouette.h"

#include <algorithm>
#include <cmath>

namespace app {

using core::Vec3;

CylinderSilhouette buildCylinderSilhouette(const core::Geometry& geo,
                                           const core::Bounds& bounds,
                                           int width, int height) {
  CylinderSilhouette out;
  out.width = width;
  out.height = height;
  out.rgba.assign((size_t)width * height * 4, 0);

  const double padPx = 18;
  const double sx = bounds.size.x, sy = bounds.size.y;
  const double halfX = std::max(sx, 1e-6) * 0.75;
  const double halfY = std::max(sy, 1e-6) * 0.75;
  const double cxw = bounds.center.x;
  const double cyw = bounds.center.y;
  const double drawW = width - padPx * 2;
  const double drawH = height - padPx * 2;
  const double scale = std::min(drawW / (halfX * 2), drawH / (halfY * 2));
  out.cxw = cxw;
  out.cyw = cyw;
  out.scale = scale;

  const auto wx2px = [&](double wx) { return (wx - cxw) * scale + width / 2.0; };
  const auto wy2py = [&](double wy) { return height / 2.0 - (wy - cyw) * scale; };

  std::vector<uint8_t> mask((size_t)width * height, 0);
  const auto& pos = geo.positions;
  const size_t triCount = pos.size() / 9;
  for (size_t t = 0; t < triCount; t++) {
    const size_t b = t * 9;
    const double x0 = wx2px(pos[b]), y0 = wy2py(pos[b + 1]);
    const double x1 = wx2px(pos[b + 3]), y1 = wy2py(pos[b + 4]);
    const double x2 = wx2px(pos[b + 6]), y2 = wy2py(pos[b + 7]);
    const int minX = std::max(0, (int)std::floor(std::min({x0, x1, x2})));
    const int maxX = std::min(width - 1, (int)std::ceil(std::max({x0, x1, x2})));
    const int minY = std::max(0, (int)std::floor(std::min({y0, y1, y2})));
    const int maxY = std::min(height - 1, (int)std::ceil(std::max({y0, y1, y2})));
    if (minX > maxX || minY > maxY) continue;
    for (int py = minY; py <= maxY; py++) {
      for (int px = minX; px <= maxX; px++) {
        const double fx = px + 0.5, fy = py + 0.5;
        const double w0 = (fx - x1) * (y2 - y1) - (fy - y1) * (x2 - x1);
        const double w1 = (fx - x2) * (y0 - y2) - (fy - y2) * (x0 - x2);
        const double w2 = (fx - x0) * (y1 - y0) - (fy - y0) * (x1 - x0);
        if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))
          mask[(size_t)py * width + px] = 1;
      }
    }
  }

  for (size_t i = 0; i < mask.size(); i++) {
    if (!mask[i]) continue;
    out.rgba[i * 4] = 110;
    out.rgba[i * 4 + 1] = 130;
    out.rgba[i * 4 + 2] = 145;
    out.rgba[i * 4 + 3] = 220;
  }
  return out;
}

bool autoFitCylinderAxis(const core::Geometry& geo,
                         const std::vector<float>& faceNormalAttr,
                         const core::Bounds& bounds, double& outCx,
                         double& outCy, double& outRadius) {
  const auto& pos = geo.positions;
  const size_t triCount = pos.size() / 9;
  if (faceNormalAttr.size() != pos.size()) return false;

  int n = 0;
  double Sx = 0, Sy = 0, Sxx = 0, Syy = 0, Sxy = 0;
  double Sxz = 0, Syz = 0, Sz = 0;
  for (size_t t = 0; t < triCount; t++) {
    const size_t b = t * 9;
    const double nz = faceNormalAttr[b + 2];
    if (std::abs(nz) >= 0.5) continue; // skip cap-like triangles
    for (int v = 0; v < 3; v++) {
      const double x = pos[b + v * 3];
      const double y = pos[b + v * 3 + 1];
      const double z = x * x + y * y;
      Sx += x; Sy += y; Sxx += x * x; Syy += y * y; Sxy += x * y;
      Sxz += x * z; Syz += y * z; Sz += z;
      n++;
    }
  }
  if (n < 10) return false;

  // Solve the 3x3 normal equations for [A, B, C] where (cx, cy) = (A/2, B/2)
  // and r = sqrt(C + cx^2 + cy^2).
  const double M[3][3] = {
      {Sxx, Sxy, Sx},
      {Sxy, Syy, Sy},
      {Sx, Sy, (double)n},
  };
  const double rhs[3] = {Sxz, Syz, Sz};
  const auto det3 = [](const double m[3][3]) {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
          m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
          m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
  };
  const double D = det3(M);
  if (std::abs(D) < 1e-12) return false;
  double R0[3][3], R1[3][3], R2[3][3];
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) {
      R0[i][j] = (j == 0) ? rhs[i] : M[i][j];
      R1[i][j] = (j == 1) ? rhs[i] : M[i][j];
      R2[i][j] = (j == 2) ? rhs[i] : M[i][j];
    }
  const double A = det3(R0) / D;
  const double B = det3(R1) / D;
  const double C = det3(R2) / D;
  const double cx = A / 2, cy = B / 2;
  const double r2 = C + cx * cx + cy * cy;
  if (!std::isfinite(r2) || r2 <= 0) return false;
  const double r = std::sqrt(r2);
  const double maxReasonable = std::max(bounds.size.x, bounds.size.y) * 5;
  if (r > maxReasonable || r < 1e-3) return false;

  outCx = cx;
  outCy = cy;
  outRadius = r;
  return true;
}

} // namespace app
