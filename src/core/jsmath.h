// Exact ports of JavaScript numeric primitives the reference app relies on.
// These differ from the "obvious" C++ equivalents in edge cases, and pipeline
// fidelity depends on matching them bit-for-bit.
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace core::js {

// JS Math.round: half-values round toward +Infinity (Math.round(-0.5) === -0,
// Math.round(-1.5) === -1). std::round rounds half away from zero instead.
// The -0 result matters: meshRepair writes Math.round(x*Q)/Q straight into
// Float32 buffers, so -0.0 vs +0.0 is an observable output bit.
inline double round(double x) {
  double r = std::floor(x + 0.5);
  if (r == 0 && (x < 0 || std::signbit(x))) return -0.0;
  return r;
}

// JS ToInt32 (the `| 0` operator): modular wrap into [-2^31, 2^31).
inline int32_t toInt32(double x) {
  if (!std::isfinite(x)) return 0;
  // C++20: conversion to uint32_t of a negative double is UB — go via fmod.
  double m = std::fmod(std::trunc(x), 4294967296.0);
  if (m < 0) m += 4294967296.0;
  uint32_t u = static_cast<uint32_t>(m);
  return static_cast<int32_t>(u);
}

// JS Math.imul: 32-bit integer multiplication with wrap.
inline int32_t imul(int32_t a, int32_t b) {
  return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}

// JS >>> : unsigned right shift (operands coerced to uint32).
inline uint32_t ushr(int32_t v, int amount) {
  return static_cast<uint32_t>(v) >> amount;
}

// JS Math.fround: round a double to nearest float32 (what a Float32Array
// store does).
inline float fround(double x) { return static_cast<float>(x); }

} // namespace core::js
