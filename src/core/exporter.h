// Port of reference/js/exporter.js — binary STL and 3MF writers.
// Produces byte buffers; callers handle saving (the browser download in the
// original becomes a native save dialog in the app).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/geometry.h"

namespace core {

// Binary STL: 80-byte zero header + uint32 triangle count + 50 bytes/triangle.
// Normals come from the geometry's normal attribute (first vertex of each
// triangle — flat shading); when absent they are computed per-face.
std::vector<uint8_t> exportSTL(const Geometry& geometry);

// 3MF: ZIP package with [Content_Types].xml, _rels/.rels and 3D/3dmodel.model.
// Vertices welded on the 1e4 grid and written with up to 4 decimals
// (trailing zeros trimmed), exactly like the original exporter.
std::vector<uint8_t> export3MF(const Geometry& geometry);

// JS Number.prototype.toFixed(4) with trailing-zero/dot trim — exposed for
// tests. JS toFixed resolves decimal ties by picking the larger integer,
// unlike printf's round-half-even.
std::string fmt3MFCoord(double n);

} // namespace core
