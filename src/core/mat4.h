// Column-major 4x4 matrix mirroring three.js Matrix4 (MIT) semantics —
// only the operations the pipeline uses.
#pragma once

#include "core/geometry.h"

namespace core {

struct Mat4 {
  // Column-major storage, same as three.js .elements
  double e[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

  static Mat4 identity() { return Mat4{}; }

  static Mat4 makeScale(double x, double y, double z) {
    Mat4 m;
    m.e[0] = x; m.e[5] = y; m.e[10] = z;
    return m;
  }

  // three.js Matrix4.set takes row-major arguments.
  static Mat4 fromRowMajor(double n11, double n12, double n13, double n14,
                           double n21, double n22, double n23, double n24,
                           double n31, double n32, double n33, double n34,
                           double n41, double n42, double n43, double n44) {
    Mat4 m;
    m.e[0] = n11; m.e[4] = n12; m.e[8]  = n13; m.e[12] = n14;
    m.e[1] = n21; m.e[5] = n22; m.e[9]  = n23; m.e[13] = n24;
    m.e[2] = n31; m.e[6] = n32; m.e[10] = n33; m.e[14] = n34;
    m.e[3] = n41; m.e[7] = n42; m.e[11] = n43; m.e[15] = n44;
    return m;
  }

  // this * other (three.js multiply)
  Mat4 multiplied(const Mat4& b) const {
    const double* ae = e;
    const double* be = b.e;
    Mat4 r;
    for (int col = 0; col < 4; col++) {
      for (int row = 0; row < 4; row++) {
        r.e[col * 4 + row] = ae[0 * 4 + row] * be[col * 4 + 0] +
                             ae[1 * 4 + row] * be[col * 4 + 1] +
                             ae[2 * 4 + row] * be[col * 4 + 2] +
                             ae[3 * 4 + row] * be[col * 4 + 3];
      }
    }
    return r;
  }

  // three.js Vector3.applyMatrix4 (perspective divide included).
  Vec3 apply(const Vec3& v) const {
    double w = 1.0 / (e[3] * v.x + e[7] * v.y + e[11] * v.z + e[15]);
    return {(e[0] * v.x + e[4] * v.y + e[8] * v.z + e[12]) * w,
            (e[1] * v.x + e[5] * v.y + e[9] * v.z + e[13]) * w,
            (e[2] * v.x + e[6] * v.y + e[10] * v.z + e[14]) * w};
  }
};

} // namespace core
