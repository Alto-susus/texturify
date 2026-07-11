// "Spectre Displace 1a" design tokens — colors, radii and fonts lifted from
// the mockup (Displacement Studio.dc.html, option 1a "Docked Studio").
#pragma once

#include <imgui.h>
#include <string>

namespace ui {

// ── Colors (mockup values) ──────────────────────────────────────────────────
namespace col {
inline constexpr ImU32 rgba(int r, int g, int b, float a) {
  return IM_COL32(r, g, b, (int)(a * 255.0f + 0.5f));
}
inline constexpr ImU32 white(float a) { return rgba(255, 255, 255, a); }

// Accent family
inline constexpr ImU32 kAccent = IM_COL32(0xff, 0x2d, 0x55, 255);   // #ff2d55
inline constexpr ImU32 kAccentHi = IM_COL32(0xff, 0x5a, 0x68, 255); // #ff5a68
inline constexpr ImU32 kAccentLo = IM_COL32(0xe0, 0x10, 0x40, 255); // #e01040
inline constexpr ImU32 kAccentDeep = IM_COL32(0xd4, 0x0f, 0x38, 255); // #d40f38
inline constexpr ImU32 kAccentText = IM_COL32(0xff, 0x8a, 0x98, 255); // #ff8a98

// Backgrounds
inline constexpr ImU32 kWindowBg = IM_COL32(0x0a, 0x0a, 0x0e, 255);   // #0a0a0e
inline constexpr ImU32 kViewportBg = IM_COL32(0x08, 0x08, 0x0b, 255); // #08080b
inline constexpr ImU32 kText = IM_COL32(0xf1, 0xef, 0xf2, 255);       // #f1eff2
inline constexpr ImU32 kGreen = IM_COL32(0x3a, 0xd0, 0x7a, 255);      // #3ad07a
inline constexpr ImU32 kAmber = IM_COL32(0xff, 0xc0, 0x4d, 255);      // warnings
} // namespace col

// ── Fonts ───────────────────────────────────────────────────────────────────
// The mockup uses Instrument Sans 400/500/600 at 10.5–14px and JetBrains Mono
// 400/500 at 10.5–11.5px. Sizes are logical pixels scaled by uiScale.
struct Fonts {
  ImFont* sans12 = nullptr;      // Regular 12.5 — body/labels
  ImFont* sansMed12 = nullptr;   // Medium 12.5 — buttons, list rows
  ImFont* sansSemi12 = nullptr;  // SemiBold 12 — tabs, chips
  ImFont* sansSemi13 = nullptr;  // SemiBold 13 — Export button
  ImFont* sansSemi14 = nullptr;  // SemiBold 14 — logo wordmark
  ImFont* sansSemi10 = nullptr;  // SemiBold 10.5 — section headers (uppercase)
  ImFont* mono11 = nullptr;      // JetBrains Mono 11 — values, stats
  ImFont* mono10 = nullptr;      // JetBrains Mono 10.5 — meta rows
};

struct Theme {
  Fonts fonts;
  float scale = 1.0f;

  // Loads fonts from <assetDir>/fonts and applies the ImGui style. `langCode`
  // picks which extra glyph ranges get merged onto every Sans weight so
  // non-English UI text doesn't render as tofu: Latin Extended-A + Cyrillic
  // are always merged (covers all 10 languages except CJK scripts), and for
  // "ja"/"ko" the matching Noto Sans JP/KR font is additionally merged with
  // ImGui's built-in Japanese/Korean glyph ranges. Safe to call again at
  // runtime to rebuild the atlas for a language change — ImGui 1.92's OpenGL3
  // backend re-uploads the font texture automatically next frame, no manual
  // device-object rebuild needed (confirmed: the existing single startup call
  // already relies on this with no explicit CreateFontsTexture call anywhere).
  bool init(const std::string& assetDir, float uiScale = 1.0f,
           const std::string& langCode = "en");

  // Kept alive for ImGui (it stores the pointer, not a copy) until the next
  // init() call rebuilds them.
  ImVector<ImWchar> mergedRanges;
  ImVector<ImWchar> cjkRanges;
};

// Rounded-corner radii
inline constexpr float kPanelRounding = 18.0f;
inline constexpr float kButtonRounding = 10.0f;
inline constexpr float kChipRounding = 9.0f;

} // namespace ui
