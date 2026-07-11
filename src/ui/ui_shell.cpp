// Layout + frame orchestration for the 1a Docked Studio shell.
#include <cstring>

#include "ui/ui.h"
#include "ui/widgets.h"

namespace ui {

std::string stripInfoIcon(std::string s) {
  static constexpr const char* kSuffix = " \xe2\x93\x98"; // " ⓘ" (U+24D8)
  const size_t n = std::strlen(kSuffix);
  if (s.size() >= n && s.compare(s.size() - n, n, kSuffix) == 0) s.resize(s.size() - n);
  return s;
}

std::string stripWarningIcon(std::string s) {
  static constexpr const char* kPrefix = "\xe2\x9a\xa0 "; // "⚠ " (U+26A0)
  const size_t n = std::strlen(kPrefix);
  if (s.size() >= n && s.compare(0, n, kPrefix) == 0) s.erase(0, n);
  return s;
}

std::string stripCheckIcon(std::string s) {
  static constexpr const char* kPrefix = "\xe2\x9c\x94 "; // "✔ " (U+2714)
  const size_t n = std::strlen(kPrefix);
  if (s.size() >= n && s.compare(0, n, kPrefix) == 0) s.erase(0, n);
  return s;
}

std::string stripHtml(const std::string& in) {
  std::string s = in;
  for (const char* tag : {"<br/>", "<br />", "<br>"}) {
    const size_t tagLen = std::strlen(tag);
    size_t pos = 0;
    while ((pos = s.find(tag, pos)) != std::string::npos) {
      s.replace(pos, tagLen, "\n");
      pos += 1;
    }
  }

  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '<') {
      if (s.compare(i, 2, "<a") == 0) {
        const size_t hrefPos = s.find("href=\"", i);
        const size_t gt = s.find('>', i);
        if (hrefPos != std::string::npos && gt != std::string::npos && hrefPos < gt) {
          const size_t hrefStart = hrefPos + 6;
          const size_t hrefEnd = s.find('"', hrefStart);
          const std::string href = s.substr(hrefStart, hrefEnd - hrefStart);
          const size_t closeTag = s.find("</a>", gt);
          const std::string text =
              closeTag != std::string::npos ? s.substr(gt + 1, closeTag - gt - 1) : "";
          const bool looksLikeUrl = text.find("http") != std::string::npos ||
                                    text.find('@') != std::string::npos;
          out += text;
          if (!looksLikeUrl) {
            out += " (";
            out += href;
            out += ")";
          }
          i = closeTag != std::string::npos ? closeTag + 4 : gt + 1;
          continue;
        }
      }
      const size_t gt = s.find('>', i);
      i = gt != std::string::npos ? gt + 1 : i + 1;
      continue;
    }
    out += s[i];
    i++;
  }
  return out;
}

Layout computeLayout(float w, float h, float scale) {
  Layout l;
  l.windowSize = ImVec2(w, h);
  const float toolbarH = 58 * scale;
  const float pad = 14 * scale, gap = 14 * scale;
  const float leftW = 290 * scale, rightW = 340 * scale;

  l.toolbarMin = ImVec2(0, 0);
  l.toolbarMax = ImVec2(w, toolbarH);
  float bodyTop = toolbarH + pad;
  float bodyBottom = h - pad;
  l.leftMin = ImVec2(pad, bodyTop);
  l.leftMax = ImVec2(pad + leftW, bodyBottom);
  l.rightMin = ImVec2(w - pad - rightW, bodyTop);
  l.rightMax = ImVec2(w - pad, bodyBottom);
  l.vpMin = ImVec2(l.leftMax.x + gap, bodyTop);
  l.vpMax = ImVec2(l.rightMin.x - gap, bodyBottom);
  return l;
}

void drawUi(UiContext& ctx) {
  const Layout& l = ctx.layout;
  GlassCompositor& glass = *ctx.glass;
  ImDrawList* bg = ImGui::GetBackgroundDrawList();

  // Window background gradient
  bg->AddImage(glass.backgroundTex(), ImVec2(0, 0), l.windowSize,
               GlassCompositor::uvTopLeft(), GlassCompositor::uvBottomRight());

  // Viewport: rounded 3D render + border
  bg->AddImageRounded(glass.viewportTex(), l.vpMin, l.vpMax,
                      GlassCompositor::uvTopLeft(),
                      GlassCompositor::uvBottomRight(), IM_COL32_WHITE,
                      kPanelRounding * ctx.theme->scale);
  bg->AddRect(l.vpMin, l.vpMax, col::white(0.07f),
              kPanelRounding * ctx.theme->scale, 0, 1.0f);

  drawToolbar(ctx);
  drawLeftPanel(ctx);
  drawRightPanel(ctx);
  drawViewportHud(ctx);
  drawCylinderPanel(ctx);
  drawDiagnosticsPanel(ctx);
  drawModals(ctx);
}

} // namespace ui
