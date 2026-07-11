// Core geometry types mirroring the JS app's data model:
// non-indexed triangle soup in Float32 buffers (three.js BufferGeometry
// equivalent), with all intermediate math done in double like JS numbers.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace core {

struct Vec3 {
  double x = 0, y = 0, z = 0;

  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
  Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
  Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }

  double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
  Vec3 cross(const Vec3& o) const {
    return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
  }
  double lengthSq() const { return x * x + y * y + z * z; }
  double length() const { return std::sqrt(lengthSq()); }
  Vec3 normalized() const {
    double l = length();
    return l > 0 ? Vec3{x / l, y / l, z / l} : Vec3{0, 0, 0};
  }
};

// Non-indexed triangle soup: positions.size() == 9 * triCount.
// normals is either empty or same length as positions.
struct Geometry {
  std::vector<float> positions;
  std::vector<float> normals;

  size_t triangleCount() const { return positions.size() / 9; }
  size_t vertexCount() const { return positions.size() / 3; }
};

struct Bounds {
  Vec3 min, max, size, center;
};

Bounds computeBounds(const Geometry& g);

} // namespace core
