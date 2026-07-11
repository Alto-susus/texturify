// Port of reference/js/meshIndex.js — shared vertex welding.
//
// Open-addressing hash table mapping grid-quantized 3D positions to integer
// ids. The probing order, hash mixing, growth policy, and rounding must match
// the JS original exactly: downstream modules depend on "first occurrence
// wins" id assignment order.
//
// Grid policy (from reference/CONTEXT.md — do not change per call site):
//   1e4 — export/3MF, meshRepair, meshValidation, exclusion, masking
//   1e5 — subdivision, regularize, displacement
//   1e6 — decimation (its own packed-key welder)
#pragma once

#include <cstdint>
#include <vector>

namespace core {

class QuantizedPointMap {
public:
  explicit QuantizedPointMap(double quant, size_t expected = 256);

  // True when the last getOrSet() inserted a new key.
  bool inserted = false;

  size_t size() const { return _size; }

  // Value stored for (x,y,z)'s grid cell, or -1 if absent.
  int32_t get(double x, double y, double z) const;

  // Return the value already stored for (x,y,z)'s grid cell; if absent, store
  // `value` and return it. `inserted` tells which case occurred.
  int32_t getOrSet(double x, double y, double z, int32_t value);

private:
  void alloc(size_t cap);
  size_t slot(double qx, double qy, double qz) const;
  void grow();

  double _quant;
  size_t _size = 0;
  size_t _cap = 0;
  uint32_t _mask = 0;
  std::vector<double> _qx, _qy, _qz;
  std::vector<int32_t> _val;
};

struct WeldResult {
  std::vector<uint32_t> vertexId;
  uint32_t uniqueCount = 0;
};

// Weld a non-indexed position buffer: assign each vertex the sequential id of
// its quantized position (first occurrence wins).
WeldResult weldVertices(const float* pos, size_t count, double quant);

} // namespace core
