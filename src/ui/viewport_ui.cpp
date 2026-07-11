// Viewport HUD (drawn over the rounded 3D view): top-left live-status pill,
// top-right mesh stats card, bottom-center view-mode chips.
#include <cstdio>

#include "ui/ui.h"
#include "ui/widgets.h"

namespace ui {

namespace {

// "1234567" → "1,234,567"
void formatThousands(char* out, size_t outSize, unsigned long long v) {
  char raw[24];
  int n = std::snprintf(raw, sizeof(raw), "%llu", v);
  int commas = (n - 1) / 3;
  size_t total = (size_t)(n + commas);
  if (total + 1 > outSize) {
    std::snprintf(out, outSize, "%llu", v);
    return;
  }
  out[total] = '\0';
  int src = n - 1, dst = (int)total - 1, digits = 0;
  while (src >= 0) {
    out[dst--] = raw[src--];
    if (++digits == 3 && src >= 0) {
      out[dst--] = ',';
      digits = 0;
    }
  }
}

// HUD glass chip backdrop: blur sample + dark tint + border (lighter than the
// rail panels — the mockup HUD chips sit on the dark viewport).
void hudGlass(ImDrawList* dl, const GlassCompositor& glass, ImVec2 mn,
              ImVec2 mx, float rounding) {
  dl->AddImageRounded(glass.blurTex(), mn, mx, glass.blurUv0(mn),
                      glass.blurUv1(mx), IM_COL32_WHITE, rounding);
  dl->AddRectFilled(mn, mx, col::rgba(12, 12, 16, 0.55f), rounding);
  dl->AddRect(mn, mx, col::white(0.08f), rounding, 0, 1.0f);
}

} // namespace

void drawViewportHud(UiContext& ctx) {
  const Layout& l = ctx.layout;
  Theme& th = *ctx.theme;
  app::AppState& st = *ctx.state;
  float scale = th.scale;
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  const float inset = 14 * scale;

  // ── Top-left status pill: glowing dot + "Displacement · Live/Baked" ──────
  {
    const char* label = st.meshDirty ? "Displacement \xc2\xb7 Live"
                                     : "Displacement \xc2\xb7 Baked";
    ImU32 dotCol = st.meshDirty ? col::kAccent : col::kGreen;
    ImGui::PushFont(th.fonts.sansSemi12);
    ImVec2 ts = ImGui::CalcTextSize(label);
    float h = 32 * scale;
    float dotR = 3.5f * scale;
    ImVec2 mn(l.vpMin.x + inset, l.vpMin.y + inset);
    ImVec2 mx(mn.x + 14 * scale + dotR * 2 + 8 * scale + ts.x + 14 * scale,
              mn.y + h);
    hudGlass(dl, *ctx.glass, mn, mx, h * 0.5f);
    ImVec2 dc(mn.x + 14 * scale + dotR, (mn.y + mx.y) * 0.5f);
    dl->AddCircleFilled(dc, dotR * 2.4f, (dotCol & 0x00ffffff) | 0x38000000);
    dl->AddCircleFilled(dc, dotR, dotCol);
    dl->AddText(ImVec2(dc.x + dotR + 8 * scale, mn.y + (h - ts.y) * 0.5f),
                col::white(0.85f), label);
    ImGui::PopFont();
  }

  // ── Top-right stats card: verts / tris (mono, right-aligned) ─────────────
  {
    char num[32], line1[48], line2[48];
    formatThousands(num, sizeof(num),
                    (unsigned long long)st.meshTriangles * 3ull);
    std::snprintf(line1, sizeof(line1), "%s verts", num);
    formatThousands(num, sizeof(num), (unsigned long long)st.meshTriangles);
    std::snprintf(line2, sizeof(line2), "%s tris", num);

    ImGui::PushFont(th.fonts.mono11);
    ImVec2 s1 = ImGui::CalcTextSize(line1);
    ImVec2 s2 = ImGui::CalcTextSize(line2);
    float textW = s1.x > s2.x ? s1.x : s2.x;
    float lineH = ImGui::GetFontSize() * 1.6f; // mockup line-height 1.6
    float padX = 12 * scale, padY = 8 * scale;
    ImVec2 mn(l.vpMax.x - inset - (textW + padX * 2), l.vpMin.y + inset);
    ImVec2 mx(l.vpMax.x - inset, mn.y + lineH * 2 + padY * 2);
    hudGlass(dl, *ctx.glass, mn, mx, 12 * scale);
    float ty = mn.y + padY + (lineH - s1.y) * 0.5f;
    dl->AddText(ImVec2(mx.x - padX - s1.x, ty), col::white(0.75f), line1);
    dl->AddText(ImVec2(mx.x - padX - s2.x, ty + lineH), col::white(0.42f),
                line2);
    ImGui::PopFont();
  }

  // ── Bottom-center view-mode chips: Solid / Wireframe / Textured ──────────
  {
    const char* names[3] = {"Solid", "Wireframe", "Textured"};
    ImGui::PushFont(th.fonts.sansSemi12);
    float chipH = 26 * scale, padX = 15 * scale, gap = 4 * scale,
          framePad = 4 * scale;
    float widths[3];
    float total = framePad * 2 + gap * 2;
    for (int i = 0; i < 3; i++) {
      widths[i] = ImGui::CalcTextSize(names[i]).x + padX * 2;
      total += widths[i];
    }
    float ph = chipH + framePad * 2;
    ImVec2 mn(l.vpMin.x + (l.vpMax.x - l.vpMin.x - total) * 0.5f,
              l.vpMax.y - inset - ph);
    ImVec2 mx(mn.x + total, mn.y + ph);

    // A window so the chips capture clicks (io.WantCaptureMouse gates viewer
    // input); sized exactly to the pill so it doesn't eat viewport drags.
    ImGui::SetNextWindowPos(mn);
    ImGui::SetNextWindowSize(ImVec2(total, ph));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##vphud_chips", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* wdl = ImGui::GetWindowDrawList();
    hudGlass(wdl, *ctx.glass, mn, mx, 13 * scale);

    // Selected mode from view state: wireframe wins, then textured preview.
    int mode = st.wireframe ? 1 : (st.previewEnabled ? 2 : 0);
    float x = mn.x + framePad;
    for (int i = 0; i < 3; i++) {
      ImVec2 cmn(x, mn.y + framePad);
      ImVec2 cmx(x + widths[i], cmn.y + chipH);
      ImGui::SetCursorScreenPos(cmn);
      ImGui::PushID(i);
      bool clicked = ImGui::InvisibleButton("##chip", ImVec2(widths[i], chipH));
      bool hovered = ImGui::IsItemHovered();
      ImGui::PopID();
      float r = kChipRounding * scale;
      if (i == mode) {
        wdl->AddRectFilled(ImVec2(cmn.x, cmn.y + 2 * scale),
                           ImVec2(cmx.x, cmx.y + 4 * scale),
                           col::rgba(255, 45, 80, 0.22f), r);
        roundedGradientV(wdl, cmn, cmx, col::kAccentHi, col::kAccentLo, r);
        wdl->AddRect(cmn, cmx, col::white(0.16f), r, 0, 1.0f);
      } else if (hovered) {
        wdl->AddRectFilled(cmn, cmx, col::white(0.06f), r);
      }
      ImVec2 ts = ImGui::CalcTextSize(names[i]);
      wdl->AddText(ImVec2(x + (widths[i] - ts.x) * 0.5f,
                          cmn.y + (chipH - ts.y) * 0.5f),
                   i == mode ? IM_COL32_WHITE : col::white(0.6f), names[i]);
      if (clicked && i != mode) {
        st.wireframe = (i == 1);
        st.previewEnabled = (i == 2);
        if (ctx.actions.viewChanged) ctx.actions.viewChanged();
      }
      x += widths[i] + gap;
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopFont();
  }

  // ── Radius-brush cursor circle (replaces the DOM #excl-brush-cursor) ─────
  // brushCursorX/Y/RadiusPx are viewport-framebuffer pixels; theme.scale
  // approximates the DPI ratio back to ImGui logical coordinates.
  if (st.brushCursorVisible) {
    const float cxp = l.vpMin.x + (float)(st.brushCursorX / scale);
    const float cyp = l.vpMin.y + (float)(st.brushCursorY / scale);
    const float r = (float)(st.brushCursorRadiusPx / scale);
    const bool erase = st.brushMode == app::BrushMode::Exclude &&
                       ImGui::GetIO().KeyShift;
    const ImU32 ring = erase ? col::rgba(153, 153, 153, 0.9f)
                             : col::rgba(255, 238, 0, 0.9f);
    dl->AddCircle(ImVec2(cxp, cyp), r, ring, 48, 1.5f * scale);
  }
}

} // namespace ui
