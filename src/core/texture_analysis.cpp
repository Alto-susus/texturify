#include "core/texture_analysis.h"

#include <cmath>

namespace core {

namespace {
constexpr double SHARP_THRESHOLD = 30; // |∇I| above this = "sharp" pixel
} // namespace

TextureAnalysis analyzeTexture(const ImageDataRGBA& imageData) {
  TextureAnalysis fallback;
  const int width = imageData.width, height = imageData.height;
  if (width < 3 || height < 3 || imageData.data.empty()) return fallback;

  const uint8_t* data = imageData.data.data();
  const int stride = width * 4;
  double sumGrad = 0;
  int64_t sharpCount = 0;
  int64_t pixelCount = 0;

  // Central differences on the red channel; skip the 1-pixel border.
  for (int y = 1; y < height - 1; y++) {
    const int rowOff = y * stride;
    for (int x = 1; x < width - 1; x++) {
      const int i = rowOff + x * 4;
      const int left = data[i - 4];
      const int right = data[i + 4];
      const int up = data[i - stride];
      const int down = data[i + stride];
      const double dx = (right - left) * 0.5;
      const double dy = (down - up) * 0.5;
      const double mag = std::sqrt(dx * dx + dy * dy);
      sumGrad += mag;
      if (mag > SHARP_THRESHOLD) sharpCount++;
      pixelCount++;
    }
  }

  TextureAnalysis r;
  r.meanGrad = sumGrad / pixelCount;
  r.sharpFrac = (double)sharpCount / pixelCount;

  if (r.sharpFrac > 0.15 || r.meanGrad > 50) r.pixelsPerEdge = 1.0;
  else if (r.sharpFrac > 0.05 || r.meanGrad > 20) r.pixelsPerEdge = 1.5;
  else if (r.meanGrad > 8) r.pixelsPerEdge = 2.5;
  else r.pixelsPerEdge = 4.0;

  return r;
}

} // namespace core
