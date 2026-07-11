// Image helpers shared by the texture path:
//  - fitDimensions / decode / resize (presetTextures.js 512px cap +
//    canvas drawImage resampling)
//  - the 3-pass separable box blur used for Texture Smoothing
//    (main.js _boxBlurH/_boxBlurV/blurCanvas Safari fallback — the app's
//    deterministic CPU blur).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/displacement.h" // ImageDataRGBA

namespace core {

inline constexpr int kTextureFitSize = 512; // longest-side cap (presetTextures SIZE)

struct FitDims {
  int w = 0, h = 0;
};

// Cap at kTextureFitSize on the longest side, preserving aspect (JS
// Math.round on each axis).
FitDims fitDimensions(int imgW, int imgH, int cap = kTextureFitSize);

// Decode PNG/JPG/etc. bytes to RGBA8 (stb_image). Empty result on failure.
ImageDataRGBA decodeImageRGBA(const uint8_t* bytes, size_t size);
ImageDataRGBA decodeImageFileRGBA(const std::string& path);

// Bilinear resize (canvas drawImage-equivalent smoothing).
ImageDataRGBA resizeImageRGBA(const ImageDataRGBA& src, int w, int h);

// decode → fitDimensions → resize, the exact loadFullPreset/loadCustomTexture
// treatment. Empty result on decode failure.
ImageDataRGBA loadTextureImageRGBA(const std::string& path);

// Approximate Gaussian blur (sigma px) in place: 3 passes of box blur with
// radius r where r(r+1) ≈ sigma². No-op when sigma <= 0.
void blurImageRGBA(ImageDataRGBA& image, double sigma);

} // namespace core
