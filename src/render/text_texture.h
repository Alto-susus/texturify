// Text → RGBA bitmap rasterizer (stb_truetype) for in-scene labels: the
// X/Y/Z axis sprites and the ground-plane dimension annotations that the JS
// viewer draws onto 2D canvases.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace render {

struct TextBitmap {
  std::vector<uint8_t> rgba; // w*h*4, row 0 = top
  int width = 0, height = 0;
};

// Loads the label font once (Instrument Sans Bold, the app's UI font — stands
// in for the original's "bold Arial"). Returns false if the font is missing.
bool initTextRasterizer(const std::string& fontPath);

// Rasterize a single line, centred in a w×h canvas like the JS
// `ctx.textAlign=center; textBaseline=middle; fillText(text, w/2, h/2)`.
TextBitmap rasterizeLabel(const std::string& text, int canvasW, int canvasH,
                          double fontPx, uint32_t rgbHex);

} // namespace render
