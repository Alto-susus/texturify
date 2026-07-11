#include "core/mesh_index.h"

#include <algorithm>
#include <cmath>

#include "core/jsmath.h"

namespace core {

QuantizedPointMap::QuantizedPointMap(double quant, size_t expected)
    : _quant(quant) {
  size_t cap = 16;
  size_t target = std::max<size_t>(
      16, static_cast<size_t>(std::ceil(static_cast<double>(expected) / 0.6)));
  while (cap < target) cap *= 2;
  alloc(cap);
}

void QuantizedPointMap::alloc(size_t cap) {
  _cap = cap;
  _mask = static_cast<uint32_t>(cap - 1);
  _qx.assign(cap, 0.0);
  _qy.assign(cap, 0.0);
  _qz.assign(cap, 0.0);
  _val.assign(cap, -1);
}

size_t QuantizedPointMap::slot(double qx, double qy, double qz) const {
  // Mix the (wrapped-to-32-bit) quantized components; equality is checked
  // against the exact f64-stored values, so hash wrapping is harmless.
  int32_t h = js::imul(js::toInt32(qx), 0x9E3779B1) ^
              js::imul(js::toInt32(qy), 0x85EBCA77) ^
              js::imul(js::toInt32(qz), 0xC2B2AE3D);
  h ^= static_cast<int32_t>(js::ushr(h, 15));
  uint32_t i = static_cast<uint32_t>(h) & _mask;
  while (_val[i] != -1) {
    if (_qx[i] == qx && _qy[i] == qy && _qz[i] == qz) return i;
    i = (i + 1) & _mask;
  }
  return i;
}

void QuantizedPointMap::grow() {
  std::vector<double> oqx = std::move(_qx), oqy = std::move(_qy), oqz = std::move(_qz);
  std::vector<int32_t> oval = std::move(_val);
  size_t ocap = _cap;
  alloc(ocap * 2);
  for (size_t i = 0; i < ocap; i++) {
    if (oval[i] == -1) continue;
    size_t s = slot(oqx[i], oqy[i], oqz[i]);
    _qx[s] = oqx[i];
    _qy[s] = oqy[i];
    _qz[s] = oqz[i];
    _val[s] = oval[i];
  }
}

int32_t QuantizedPointMap::get(double x, double y, double z) const {
  size_t i = slot(js::round(x * _quant), js::round(y * _quant), js::round(z * _quant));
  return _val[i];
}

int32_t QuantizedPointMap::getOrSet(double x, double y, double z, int32_t value) {
  double qx = js::round(x * _quant), qy = js::round(y * _quant), qz = js::round(z * _quant);
  size_t i = slot(qx, qy, qz);
  int32_t existing = _val[i];
  if (existing != -1) {
    inserted = false;
    return existing;
  }
  _qx[i] = qx;
  _qy[i] = qy;
  _qz[i] = qz;
  _val[i] = value;
  inserted = true;
  if (++_size > _cap * 0.7) grow();
  return value;
}

WeldResult weldVertices(const float* pos, size_t count, double quant) {
  QuantizedPointMap map(quant, std::min(count, static_cast<size_t>(1) << 22));
  WeldResult r;
  r.vertexId.resize(count);
  uint32_t nextId = 0;
  for (size_t i = 0; i < count; i++) {
    int32_t id = map.getOrSet(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2],
                              static_cast<int32_t>(nextId));
    if (map.inserted) nextId++;
    r.vertexId[i] = static_cast<uint32_t>(id);
  }
  r.uniqueCount = nextId;
  return r;
}

} // namespace core
