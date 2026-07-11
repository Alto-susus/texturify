// Port of reference/js/textureAnalysis.js — gradient statistics that classify
// a height map as smooth/medium/sharp and yield a pixels-per-edge value.
#pragma once

#include "core/displacement.h" // ImageDataRGBA

namespace core {

struct TextureAnalysis {
  double meanGrad = 0;
  double sharpFrac = 0;
  double pixelsPerEdge = 4.0;
};

// Callers memoise per texture (the JS uses a WeakMap keyed by ImageData).
TextureAnalysis analyzeTexture(const ImageDataRGBA& imageData);

} // namespace core
