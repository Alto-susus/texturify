#include "render/text_texture.h"

#include <cmath>
#include <cstdio>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace render {

namespace {
std::vector<uint8_t> g_fontData;
stbtt_fontinfo g_font;
bool g_ready = false;
} // namespace

bool initTextRasterizer(const std::string& fontPath) {
  if (g_ready) return true;
  FILE* f = std::fopen(fontPath.c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  g_fontData.resize((size_t)size);
  size_t got = std::fread(g_fontData.data(), 1, (size_t)size, f);
  std::fclose(f);
  if (got != (size_t)size) return false;
  if (!stbtt_InitFont(&g_font, g_fontData.data(),
                      stbtt_GetFontOffsetForIndex(g_fontData.data(), 0)))
    return false;
  g_ready = true;
  return true;
}

TextBitmap rasterizeLabel(const std::string& text, int canvasW, int canvasH,
                          double fontPx, uint32_t rgbHex) {
  TextBitmap out;
  out.width = canvasW;
  out.height = canvasH;
  out.rgba.assign((size_t)canvasW * canvasH * 4, 0);
  if (!g_ready || text.empty()) return out;

  float scale = stbtt_ScaleForPixelHeight(&g_font, (float)fontPx);
  int ascent, descent, lineGap;
  stbtt_GetFontVMetrics(&g_font, &ascent, &descent, &lineGap);

  // Measure advance width for centering.
  double totalW = 0;
  for (size_t i = 0; i < text.size(); i++) {
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&g_font, (unsigned char)text[i], &adv, &lsb);
    totalW += adv * scale;
    if (i + 1 < text.size())
      totalW += scale * stbtt_GetCodepointKernAdvance(
                            &g_font, (unsigned char)text[i],
                            (unsigned char)text[i + 1]);
  }

  // textBaseline=middle: baseline sits half the cap-ish height below centre.
  double x = canvasW / 2.0 - totalW / 2.0;
  double baseline = canvasH / 2.0 + (ascent + descent) * scale / 2.0;

  const uint8_t r = (rgbHex >> 16) & 0xFF, g = (rgbHex >> 8) & 0xFF,
                b = rgbHex & 0xFF;

  for (size_t i = 0; i < text.size(); i++) {
    int cp = (unsigned char)text[i];
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&g_font, cp, &adv, &lsb);
    int x0, y0, x1, y1;
    float xShift = (float)(x - std::floor(x));
    stbtt_GetCodepointBitmapBoxSubpixel(&g_font, cp, scale, scale, xShift, 0,
                                        &x0, &y0, &x1, &y1);
    int gw = x1 - x0, gh = y1 - y0;
    if (gw > 0 && gh > 0) {
      std::vector<uint8_t> gray((size_t)gw * gh);
      stbtt_MakeCodepointBitmapSubpixel(&g_font, gray.data(), gw, gh, gw,
                                        scale, scale, xShift, 0, cp);
      int ox = (int)std::floor(x) + x0;
      int oy = (int)std::floor(baseline) + y0;
      for (int gy = 0; gy < gh; gy++) {
        int py = oy + gy;
        if (py < 0 || py >= canvasH) continue;
        for (int gx = 0; gx < gw; gx++) {
          int px = ox + gx;
          if (px < 0 || px >= canvasW) continue;
          uint8_t a = gray[(size_t)gy * gw + gx];
          if (!a) continue;
          size_t o = ((size_t)py * canvasW + px) * 4;
          out.rgba[o] = r;
          out.rgba[o + 1] = g;
          out.rgba[o + 2] = b;
          if (a > out.rgba[o + 3]) out.rgba[o + 3] = a;
        }
      }
    }
    x += adv * scale;
    if (i + 1 < text.size())
      x += scale * stbtt_GetCodepointKernAdvance(&g_font, cp,
                                                 (unsigned char)text[i + 1]);
  }
  return out;
}

} // namespace render
