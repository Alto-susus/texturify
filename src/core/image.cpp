#include "core/image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/jsmath.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

namespace core {

FitDims fitDimensions(int imgW, int imgH, int cap) {
  double scale =
      std::min({(double)cap / imgW, (double)cap / imgH, 1.0});
  return {(int)js::round(imgW * scale), (int)js::round(imgH * scale)};
}

ImageDataRGBA decodeImageRGBA(const uint8_t* bytes, size_t size) {
  ImageDataRGBA out;
  int w = 0, h = 0, comp = 0;
  stbi_uc* px = stbi_load_from_memory(bytes, (int)size, &w, &h, &comp, 4);
  if (!px) return out;
  out.width = w;
  out.height = h;
  out.data.assign(px, px + (size_t)w * h * 4);
  stbi_image_free(px);
  return out;
}

ImageDataRGBA decodeImageFileRGBA(const std::string& path) {
  ImageDataRGBA out;
  int w = 0, h = 0, comp = 0;
  stbi_uc* px = stbi_load(path.c_str(), &w, &h, &comp, 4);
  if (!px) return out;
  out.width = w;
  out.height = h;
  out.data.assign(px, px + (size_t)w * h * 4);
  stbi_image_free(px);
  return out;
}

ImageDataRGBA resizeImageRGBA(const ImageDataRGBA& src, int w, int h) {
  if (w <= 0 || h <= 0 || src.data.empty()) return {};
  if (w == src.width && h == src.height) return src;
  ImageDataRGBA out;
  out.width = w;
  out.height = h;
  out.data.resize((size_t)w * h * 4);
  // Canvas drawImage filters premultiplied pixels in sRGB space with no
  // gamma linearization; imageSmoothingQuality default = bilinear.
  stbir_resize(src.data.data(), src.width, src.height, 0, out.data.data(), w,
               h, 0, STBIR_RGBA, STBIR_TYPE_UINT8, STBIR_EDGE_CLAMP,
               STBIR_FILTER_TRIANGLE);
  return out;
}

ImageDataRGBA loadTextureImageRGBA(const std::string& path) {
  ImageDataRGBA decoded = decodeImageFileRGBA(path);
  if (decoded.data.empty()) return decoded;
  FitDims fit = fitDimensions(decoded.width, decoded.height);
  return resizeImageRGBA(decoded, fit.w, fit.h);
}

namespace {

// main.js _boxBlurH — horizontal pass with clamped edge sampling.
void boxBlurH(const uint8_t* src, uint8_t* dst, int w, int h, int r) {
  const double iarr = 1.0 / (2 * r + 1);
  for (int y = 0; y < h; y++) {
    const int64_t row = (int64_t)y * w;
    for (int ch = 0; ch < 4; ch++) {
      double val = 0;
      for (int x = -r; x <= r; x++)
        val += src[(row + std::max(0, std::min(x, w - 1))) * 4 + ch];
      for (int x = 0; x < w; x++) {
        val += src[(row + std::min(x + r, w - 1)) * 4 + ch] -
               src[(row + std::max(x - r - 1, 0)) * 4 + ch];
        dst[(row + x) * 4 + ch] = (uint8_t)js::round(val * iarr);
      }
    }
  }
}

// main.js _boxBlurV — vertical pass.
void boxBlurV(const uint8_t* src, uint8_t* dst, int w, int h, int r) {
  const double iarr = 1.0 / (2 * r + 1);
  for (int x = 0; x < w; x++) {
    for (int ch = 0; ch < 4; ch++) {
      double val = 0;
      for (int y = -r; y <= r; y++)
        val += src[((int64_t)std::max(0, std::min(y, h - 1)) * w + x) * 4 + ch];
      for (int y = 0; y < h; y++) {
        val += src[((int64_t)std::min(y + r, h - 1) * w + x) * 4 + ch] -
               src[((int64_t)std::max(y - r - 1, 0) * w + x) * 4 + ch];
        dst[((int64_t)y * w + x) * 4 + ch] = (uint8_t)js::round(val * iarr);
      }
    }
  }
}

} // namespace

void blurImageRGBA(ImageDataRGBA& image, double sigma) {
  if (sigma <= 0 || image.data.empty()) return;
  // 3 passes of box blur ≈ Gaussian; radius r where r(r+1) ≈ sigma².
  int r = std::max(
      1, (int)js::round((std::sqrt(4 * sigma * sigma + 1) - 1) / 2));
  std::vector<uint8_t> b(image.data.size());
  uint8_t* a = image.data.data();
  const int w = image.width, h = image.height;
  for (int pass = 0; pass < 3; pass++) {
    boxBlurH(a, b.data(), w, h, r);
    boxBlurV(b.data(), a, w, h, r);
  }
}

} // namespace core
