// 3D math for the viewer — quaternions, view/projection matrices, rays.
// Mirrors the three.js (MIT) member semantics the reference viewer uses.
// Double precision throughout; converted to float only at GL upload.
#pragma once

#include <cmath>

#include "core/geometry.h" // Vec3
#include "core/mat4.h"

namespace render {

using core::Mat4;
using core::Vec3;

constexpr double kPi = 3.14159265358979323846;
inline double degToRad(double d) { return d * kPi / 180.0; }
inline double radToDeg(double r) { return r * 180.0 / kPi; }

struct Quat {
  double x = 0, y = 0, z = 0, w = 1;

  static Quat fromAxisAngle(const Vec3& axis, double angle) {
    double h = angle / 2, s = std::sin(h);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(h)};
  }

  // three.js setFromUnitVectors (from/to normalized).
  static Quat fromUnitVectors(const Vec3& from, const Vec3& to) {
    double r = from.dot(to) + 1;
    Quat q;
    if (r < 1e-10) {
      // 180°: pick the most orthogonal axis
      r = 0;
      if (std::abs(from.x) > std::abs(from.z)) {
        q.x = -from.y; q.y = from.x; q.z = 0; q.w = 0;
      } else {
        q.x = 0; q.y = -from.z; q.z = from.y; q.w = 0;
      }
    } else {
      Vec3 c = from.cross(to);
      q.x = c.x; q.y = c.y; q.z = c.z; q.w = r;
    }
    return q.normalized();
  }

  // three.js setFromEuler(x, y, z, 'XYZ'), radians.
  static Quat fromEulerXYZ(double ex, double ey, double ez) {
    double c1 = std::cos(ex / 2), c2 = std::cos(ey / 2), c3 = std::cos(ez / 2);
    double s1 = std::sin(ex / 2), s2 = std::sin(ey / 2), s3 = std::sin(ez / 2);
    return {s1 * c2 * c3 + c1 * s2 * s3,
            c1 * s2 * c3 - s1 * c2 * s3,
            c1 * c2 * s3 + s1 * s2 * c3,
            c1 * c2 * c3 - s1 * s2 * s3};
  }

  // this = q * this (three.js premultiply)
  Quat premultiplied(const Quat& q) const { return q.multiplied(*this); }

  // this * b (three.js multiply)
  Quat multiplied(const Quat& b) const {
    return {w * b.x + x * b.w + y * b.z - z * b.y,
            w * b.y + y * b.w + z * b.x - x * b.z,
            w * b.z + z * b.w + x * b.y - y * b.x,
            w * b.w - x * b.x - y * b.y - z * b.z};
  }

  Quat normalized() const {
    double l = std::sqrt(x * x + y * y + z * z + w * w);
    if (l == 0) return {0, 0, 0, 1};
    return {x / l, y / l, z / l, w / l};
  }

  Vec3 rotate(const Vec3& v) const {
    // t = 2 q_v × v; v' = v + w t + q_v × t
    Vec3 qv{x, y, z};
    Vec3 t = qv.cross(v) * 2.0;
    return v + t * w + qv.cross(t);
  }

  Mat4 toMat4() const {
    double x2 = x + x, y2 = y + y, z2 = z + z;
    double xx = x * x2, xy = x * y2, xz = x * z2;
    double yy = y * y2, yz = y * z2, zz = z * z2;
    double wx = w * x2, wy = w * y2, wz = w * z2;
    return Mat4::fromRowMajor(1 - (yy + zz), xy - wz, xz + wy, 0,
                              xy + wz, 1 - (xx + zz), yz - wx, 0,
                              xz - wy, yz + wx, 1 - (xx + yy), 0,
                              0, 0, 0, 1);
  }

  // three.js setFromRotationMatrix (pure rotation matrix expected).
  static Quat fromMat4(const Mat4& m);
};

inline Quat Quat::fromMat4(const Mat4& m) {
  const double* te = m.e;
  double m11 = te[0], m12 = te[4], m13 = te[8];
  double m21 = te[1], m22 = te[5], m23 = te[9];
  double m31 = te[2], m32 = te[6], m33 = te[10];
  double trace = m11 + m22 + m33;
  Quat q;
  if (trace > 0) {
    double s = 0.5 / std::sqrt(trace + 1.0);
    q.w = 0.25 / s;
    q.x = (m32 - m23) * s;
    q.y = (m13 - m31) * s;
    q.z = (m21 - m12) * s;
  } else if (m11 > m22 && m11 > m33) {
    double s = 2.0 * std::sqrt(1.0 + m11 - m22 - m33);
    q.w = (m32 - m23) / s;
    q.x = 0.25 * s;
    q.y = (m12 + m21) / s;
    q.z = (m13 + m31) / s;
  } else if (m22 > m33) {
    double s = 2.0 * std::sqrt(1.0 + m22 - m11 - m33);
    q.w = (m13 - m31) / s;
    q.x = (m12 + m21) / s;
    q.y = 0.25 * s;
    q.z = (m23 + m32) / s;
  } else {
    double s = 2.0 * std::sqrt(1.0 + m33 - m11 - m22);
    q.w = (m21 - m12) / s;
    q.x = (m13 + m31) / s;
    q.y = (m23 + m32) / s;
    q.z = 0.25 * s;
  }
  return q;
}

// three.js Matrix4.lookAt (rotation part only; eye/target/up).
inline Mat4 lookAtRotation(const Vec3& eye, const Vec3& target, const Vec3& up) {
  Vec3 z = eye - target;
  double zl = z.length();
  if (zl == 0) z = {0, 0, 1};
  else z = z * (1.0 / zl);
  Vec3 x = up.cross(z);
  double xl = x.length();
  if (xl == 0) {
    // up parallel to z — nudge like three.js
    if (std::abs(up.z) == 1) z.x += 0.0001;
    else z.z += 0.0001;
    z = z.normalized();
    x = up.cross(z);
    xl = x.length();
  }
  x = x * (1.0 / xl);
  Vec3 y = z.cross(x);
  return Mat4::fromRowMajor(x.x, y.x, z.x, 0,
                            x.y, y.y, z.y, 0,
                            x.z, y.z, z.z, 0,
                            0, 0, 0, 1);
}

inline Mat4 makeTranslation(const Vec3& t) {
  Mat4 m;
  m.e[12] = t.x; m.e[13] = t.y; m.e[14] = t.z;
  return m;
}

// nearZ/farZ, not near/far — those are empty macros in <windows.h>.
inline Mat4 perspectiveMatrix(double fovDeg, double aspect, double nearZ,
                              double farZ) {
  double top = nearZ * std::tan(degToRad(fovDeg / 2));
  double height = 2 * top, width = aspect * height;
  double left = -width / 2, right = left + width, bottom = top - height;
  Mat4 m;
  m.e[0] = 2 * nearZ / (right - left);
  m.e[5] = 2 * nearZ / (top - bottom);
  m.e[8] = (right + left) / (right - left);
  m.e[9] = (top + bottom) / (top - bottom);
  m.e[10] = -(farZ + nearZ) / (farZ - nearZ);
  m.e[11] = -1;
  m.e[14] = -2 * farZ * nearZ / (farZ - nearZ);
  m.e[15] = 0;
  return m;
}

inline Mat4 orthoMatrix(double left, double right, double top, double bottom,
                        double nearZ, double farZ) {
  Mat4 m;
  m.e[0] = 2 / (right - left);
  m.e[5] = 2 / (top - bottom);
  m.e[10] = -2 / (farZ - nearZ);
  m.e[12] = -(right + left) / (right - left);
  m.e[13] = -(top + bottom) / (top - bottom);
  m.e[14] = -(farZ + nearZ) / (farZ - nearZ);
  return m;
}

// General 4x4 inverse (three.js Matrix4.invert).
inline Mat4 inverted(const Mat4& m) {
  const double* te = m.e;
  double n11 = te[0], n21 = te[1], n31 = te[2], n41 = te[3];
  double n12 = te[4], n22 = te[5], n32 = te[6], n42 = te[7];
  double n13 = te[8], n23 = te[9], n33 = te[10], n43 = te[11];
  double n14 = te[12], n24 = te[13], n34 = te[14], n44 = te[15];

  double t11 = n23*n34*n42 - n24*n33*n42 + n24*n32*n43 - n22*n34*n43 - n23*n32*n44 + n22*n33*n44;
  double t12 = n14*n33*n42 - n13*n34*n42 - n14*n32*n43 + n12*n34*n43 + n13*n32*n44 - n12*n33*n44;
  double t13 = n13*n24*n42 - n14*n23*n42 + n14*n22*n43 - n12*n24*n43 - n13*n22*n44 + n12*n23*n44;
  double t14 = n14*n23*n32 - n13*n24*n32 - n14*n22*n33 + n12*n24*n33 + n13*n22*n34 - n12*n23*n34;

  double det = n11 * t11 + n21 * t12 + n31 * t13 + n41 * t14;
  Mat4 r;
  if (det == 0) { for (int i = 0; i < 16; i++) r.e[i] = 0; return r; }
  double di = 1.0 / det;

  r.e[0] = t11 * di;
  r.e[1] = (n24*n33*n41 - n23*n34*n41 - n24*n31*n43 + n21*n34*n43 + n23*n31*n44 - n21*n33*n44) * di;
  r.e[2] = (n22*n34*n41 - n24*n32*n41 + n24*n31*n42 - n21*n34*n42 - n22*n31*n44 + n21*n32*n44) * di;
  r.e[3] = (n23*n32*n41 - n22*n33*n41 - n23*n31*n42 + n21*n33*n42 + n22*n31*n43 - n21*n32*n43) * di;
  r.e[4] = t12 * di;
  r.e[5] = (n13*n34*n41 - n14*n33*n41 + n14*n31*n43 - n11*n34*n43 - n13*n31*n44 + n11*n33*n44) * di;
  r.e[6] = (n14*n32*n41 - n12*n34*n41 - n14*n31*n42 + n11*n34*n42 + n12*n31*n44 - n11*n32*n44) * di;
  r.e[7] = (n12*n33*n41 - n13*n32*n41 + n13*n31*n42 - n11*n33*n42 - n12*n31*n43 + n11*n32*n43) * di;
  r.e[8] = t13 * di;
  r.e[9] = (n14*n23*n41 - n13*n24*n41 - n14*n21*n43 + n11*n24*n43 + n13*n21*n44 - n11*n23*n44) * di;
  r.e[10] = (n12*n24*n41 - n14*n22*n41 + n14*n21*n42 - n11*n24*n42 - n12*n21*n44 + n11*n22*n44) * di;
  r.e[11] = (n13*n22*n41 - n12*n23*n41 - n13*n21*n42 + n11*n23*n42 + n12*n21*n43 - n11*n22*n43) * di;
  r.e[12] = t14 * di;
  r.e[13] = (n13*n24*n31 - n14*n23*n31 + n14*n21*n33 - n11*n24*n33 - n13*n21*n34 + n11*n23*n34) * di;
  r.e[14] = (n14*n22*n31 - n12*n24*n31 - n14*n21*n32 + n11*n24*n32 + n12*n21*n34 - n11*n22*n34) * di;
  r.e[15] = (n12*n23*n31 - n13*n22*n31 + n13*n21*n32 - n11*n23*n32 - n12*n21*n33 + n11*n22*n33) * di;
  return r;
}

// Upper-left 3x3 of inverse-transpose (three.js normalMatrix), column-major.
inline void normalMatrix3(const Mat4& modelView, float out9[9]) {
  Mat4 inv = inverted(modelView);
  // transpose of inverse: out[col*3+row] = inv[row*4+col]
  for (int col = 0; col < 3; col++)
    for (int row = 0; row < 3; row++)
      out9[col * 3 + row] = (float)inv.e[row * 4 + col];
}

struct Ray {
  Vec3 origin, dir; // dir normalized

  // Möller–Trumbore; returns t ≥ 0 or -1. Double-sided.
  double intersectTriangle(const Vec3& a, const Vec3& b, const Vec3& c) const {
    Vec3 e1 = b - a, e2 = c - a;
    Vec3 p = dir.cross(e2);
    double det = e1.dot(p);
    if (std::abs(det) < 1e-12) return -1;
    double invDet = 1.0 / det;
    Vec3 tv = origin - a;
    double u = tv.dot(p) * invDet;
    if (u < 0 || u > 1) return -1;
    Vec3 q = tv.cross(e1);
    double v = dir.dot(q) * invDet;
    if (v < 0 || u + v > 1) return -1;
    double t = e2.dot(q) * invDet;
    return t >= 0 ? t : -1;
  }

  bool intersectPlane(const Vec3& planeNormal, const Vec3& planePoint,
                      Vec3& out) const {
    double denom = planeNormal.dot(dir);
    if (std::abs(denom) < 1e-12) return false;
    double t = planeNormal.dot(planePoint - origin) / denom;
    if (t < 0) return false;
    out = origin + dir * t;
    return true;
  }
};

inline void mat4ToFloat(const Mat4& m, float out16[16]) {
  for (int i = 0; i < 16; i++) out16[i] = (float)m.e[i];
}

} // namespace render
