// Floating "Mesh Diagnostics" popup — port of main.js's #mesh-diagnostics
// card (main.js:3161-3208, style.css:1041-1163). Fast checks (open/
// non-manifold edges, shell count) are shown automatically after every full
// mesh reload; "Run Advanced Checks" triggers an on-demand background scan
// for intersecting/overlapping triangles. Each finding row has a Show/Hide
// toggle that drives a viewport overlay (ModelSession::toggleDiagHighlight).
// Position mirrors applyDiagSeverity()'s `diag-corner-tr` class: bottom-left
// while everything is OK, top-right (attention corner) once there's a
// warning or error.
#include <cstdio>

#include "ui/ui.h"
#include "ui/widgets.h"

namespace ui {

namespace {

constexpr ImU32 kDanger = IM_COL32(0xff, 0x5f, 0x5f, 255); // --danger (dark theme)

struct DiagLine {
  char text[112];
  app::DiagHighlight kind; // None = no Show/Hide button (the "all ok" line)
};

int buildFastLines(UiContext& ctx, const app::AppState& st, DiagLine* out) {
  int n = 0;
  if (st.diagOpenEdges == 0 && st.diagNonManifoldEdges == 0 && st.diagShellCount <= 1) {
    std::snprintf(out[n].text, sizeof(out[n].text), "%s",
                  stripCheckIcon(T(ctx, "diag.meshOk")).c_str());
    out[n++].kind = app::DiagHighlight::None;
    return n;
  }
  if (st.diagOpenEdges > 0) {
    std::snprintf(out[n].text, sizeof(out[n].text), "%s",
                  T(ctx, "diag.openEdges", {{"n", std::to_string(st.diagOpenEdges)}}).c_str());
    out[n++].kind = app::DiagHighlight::OpenEdges;
  }
  if (st.diagNonManifoldEdges > 0) {
    std::snprintf(out[n].text, sizeof(out[n].text), "%s",
                  T(ctx, "diag.nonManifoldEdges",
                    {{"n", std::to_string(st.diagNonManifoldEdges)}}).c_str());
    out[n++].kind = app::DiagHighlight::NonManifold;
  }
  if (st.diagShellCount > 1) {
    std::snprintf(out[n].text, sizeof(out[n].text), "%s",
                  T(ctx, "diag.multipleShells", {{"n", std::to_string(st.diagShellCount)}}).c_str());
    out[n++].kind = app::DiagHighlight::Shells;
  }
  return n;
}

int buildAdvancedLines(UiContext& ctx, const app::AppState& st, DiagLine* out) {
  int n = 0;
  if (st.diagIntersectingPairs == 0 && st.diagOverlappingPairs == 0) {
    std::snprintf(out[n].text, sizeof(out[n].text), "%s",
                  stripCheckIcon(T(ctx, "diag.advancedOk")).c_str());
    out[n++].kind = app::DiagHighlight::None;
    return n;
  }
  if (st.diagIntersectingPairs > 0) {
    std::snprintf(out[n].text, sizeof(out[n].text), "%s",
                  T(ctx, "diag.intersectingTris",
                    {{"n", std::to_string(st.diagIntersectingPairs)}}).c_str());
    out[n++].kind = app::DiagHighlight::Intersects;
  }
  if (st.diagOverlappingPairs > 0) {
    std::snprintf(out[n].text, sizeof(out[n].text), "%s",
                  T(ctx, "diag.overlappingTris",
                    {{"n", std::to_string(st.diagOverlappingPairs)}}).c_str());
    out[n++].kind = app::DiagHighlight::Overlaps;
  }
  return n;
}

} // namespace

void drawDiagnosticsPanel(UiContext& ctx) {
  app::AppState& st = *ctx.state;
  if (!st.diagHasFast || st.diagDismissed) return;

  Theme& th = *ctx.theme;
  const Layout& l = ctx.layout;
  const float scale = th.scale;

  const std::string tip = stripHtml(T(ctx, "diag.recommendFix"));

  DiagLine fastLines[3];
  const int fastCount = buildFastLines(ctx, st, fastLines);
  const bool fastHasIssue = fastLines[0].kind != app::DiagHighlight::None;

  DiagLine advLines[2];
  int advCount = 0;
  bool advHasIssue = false;
  if (st.diagHasAdvanced) {
    advCount = buildAdvancedLines(ctx, st, advLines);
    advHasIssue = advLines[0].kind != app::DiagHighlight::None;
  }

  // Severity: 'error' > 'warn' > 'ok' — port of applyDiagSeverity().
  int severity = 0;
  if (st.diagOpenEdges > 0 || st.diagNonManifoldEdges > 0) severity = 2;
  else if (st.diagShellCount > 1) severity = 1;
  if (st.diagHasAdvanced) {
    if (st.diagIntersectingPairs > 0) severity = 2;
    else if (st.diagOverlappingPairs > 0 && severity != 2) severity = 1;
  }
  const ImU32 sevColor = severity == 2 ? kDanger : severity == 1 ? col::kAmber : col::kGreen;
  const ImU32 sevDim = (sevColor & 0x00ffffff) | 0xcc000000;

  // ── Precompute panel height from content, so it can be anchored bottom-left
  // (ok) or top-right (warn/error) before drawing a single pixel. ─────────
  const float padX = 14 * scale, padTop = 10 * scale, padBottom = 10 * scale;
  const float rowGap = 6 * scale;
  const float panelW = 360 * scale;
  const float wrapWidth = panelW - padX * 2;

  ImGui::PushFont(th.fonts.sans12);
  const float lineH = ImGui::GetFontSize() * 1.6f;
  ImGui::PopFont();
  ImGui::PushFont(th.fonts.mono10);
  const float tipH = ImGui::CalcTextSize(tip.c_str(), nullptr, false, wrapWidth).y + 4 * scale;
  const float btnRowH = ImGui::GetFontSize() + 10 * scale;
  ImGui::PopFont();

  float contentH = fastCount * lineH + (fastHasIssue ? tipH : 0) + rowGap + btnRowH;
  if (st.diagHasAdvanced)
    contentH += rowGap + advCount * lineH + (advHasIssue ? tipH : 0);

  const float panelH = padTop + contentH + padBottom;
  const float inset = 14 * scale;
  ImVec2 mn = severity == 0
                  ? ImVec2(l.vpMin.x + inset, l.vpMax.y - inset - panelH)
                  : ImVec2(l.vpMax.x - inset - panelW, l.vpMin.y + inset);
  ImVec2 mx(mn.x + panelW, mn.y + panelH);

  ImGui::SetNextWindowPos(mn);
  ImGui::SetNextWindowSize(ImVec2(panelW, panelH));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("##diagpanel", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImDrawList* dl = ImGui::GetWindowDrawList();

  dl->AddRectFilled(mn, mx, col::rgba(14, 16, 20, 0.92f), 10 * scale);
  dl->AddRectFilled(mn, mx, (sevColor & 0x00ffffff) | 0x22000000, 10 * scale);
  dl->AddRect(mn, mx, sevColor, 10 * scale, 0, 1.0f);

  // Dismiss (×) button, top-right corner (mesh-diag-dismiss).
  {
    const float bs = 18 * scale;
    const ImVec2 bmn(mx.x - 6 * scale - bs, mn.y + 4 * scale);
    const ImVec2 bmx(bmn.x + bs, bmn.y + bs);
    ImGui::SetCursorScreenPos(bmn);
    ImGui::InvisibleButton("##diagdismiss", ImVec2(bs, bs));
    const bool hov = ImGui::IsItemHovered();
    ImGui::PushFont(th.fonts.sansSemi12);
    const char* x = "\xc3\x97"; // ×
    ImVec2 ts = ImGui::CalcTextSize(x);
    dl->AddText(ImVec2(bmn.x + (bs - ts.x) * 0.5f, bmn.y + (bs - ts.y) * 0.5f),
               hov ? col::white(1.0f) : sevDim, x);
    ImGui::PopFont();
    if (ImGui::IsItemClicked() && ctx.actions.dismissDiagnostics)
      ctx.actions.dismissDiagnostics();
  }

  float y = mn.y + padTop;
  const float textRight = mx.x - padX;

  auto drawLine = [&](const DiagLine& ln) {
    // Small hand-drawn icon in place of main.js's "⚠"/"✔" glyphs — the
    // loaded font only covers Basic Latin + Latin-1 (no Misc Symbols block).
    float textX = mn.x + padX;
    {
      const float s = 5 * scale;
      const float cx = textX + s, cy = y + lineH * 0.5f;
      if (ln.kind != app::DiagHighlight::None)
        dl->AddTriangleFilled(ImVec2(cx, cy - s), ImVec2(cx - s, cy + s * 0.8f),
                              ImVec2(cx + s, cy + s * 0.8f), sevColor);
      else {
        dl->AddLine(ImVec2(cx - s, cy), ImVec2(cx - s * 0.2f, cy + s * 0.8f),
                   sevColor, 1.6f * scale);
        dl->AddLine(ImVec2(cx - s * 0.2f, cy + s * 0.8f), ImVec2(cx + s, cy - s * 0.6f),
                   sevColor, 1.6f * scale);
      }
      textX += s * 2 + 8 * scale;
    }
    ImVec2 ts = ImGui::CalcTextSize(ln.text);
    dl->AddText(ImVec2(textX, y + (lineH - ts.y) * 0.5f), sevColor, ln.text);
    if (ln.kind != app::DiagHighlight::None) {
      const bool active = st.diagActiveHighlight == ln.kind;
      const std::string label = T(ctx, active ? "diag.hide" : "diag.show");
      ImGui::PushFont(th.fonts.mono10);
      ImVec2 bs2 = ImGui::CalcTextSize(label.c_str());
      const float bw = bs2.x + 12 * scale, bh = lineH - 4 * scale;
      const ImVec2 bmn(textRight - bw, y + (lineH - bh) * 0.5f);
      const ImVec2 bmx(bmn.x + bw, bmn.y + bh);
      ImGui::SetCursorScreenPos(bmn);
      ImGui::PushID(&ln);
      ImGui::InvisibleButton("##diagshow", ImVec2(bw, bh));
      ImGui::PopID();
      const bool hov = ImGui::IsItemHovered();
      dl->AddRect(bmn, bmx, hov ? sevColor : sevDim, 4 * scale);
      dl->AddText(ImVec2(bmn.x + (bw - bs2.x) * 0.5f, bmn.y + (bh - bs2.y) * 0.5f),
                 sevColor, label.c_str());
      ImGui::PopFont();
      if (ImGui::IsItemClicked() && ctx.actions.toggleDiagHighlight)
        ctx.actions.toggleDiagHighlight(ln.kind);
    }
    y += lineH;
  };

  auto drawTip = [&]() {
    ImGui::PushFont(th.fonts.mono10);
    ImGui::PushStyleColor(ImGuiCol_Text, sevDim);
    ImGui::SetCursorScreenPos(ImVec2(mn.x + padX, y));
    // PushTextWrapPos takes a *window-local* X, not an absolute screen
    // coordinate — window padding is pushed to (0,0) above, so local X ==
    // offset from mn.x.
    ImGui::PushTextWrapPos(padX + wrapWidth);
    ImGui::TextUnformatted(tip.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
    y += tipH;
  };

  ImGui::PushFont(th.fonts.sans12);
  for (int i = 0; i < fastCount; i++) drawLine(fastLines[i]);
  ImGui::PopFont();
  if (fastHasIssue) drawTip();

  y += rowGap;
  // ── "Run Advanced Checks" row (mesh-diag-run-btn + spinner) ─────────────
  {
    ImGui::PushFont(th.fonts.mono10);
    const std::string label = T(ctx, st.diagAdvancedRunning ? "diag.running" : "diag.runAdvanced");
    ImVec2 ts = ImGui::CalcTextSize(label.c_str());
    const float bw = ts.x + 20 * scale, bh = btnRowH;
    const ImVec2 bmn(mn.x + padX, y);
    const ImVec2 bmx(bmn.x + bw, bmn.y + bh);
    ImGui::SetCursorScreenPos(bmn);
    bool clicked = false;
    if (!st.diagAdvancedRunning)
      clicked = ImGui::InvisibleButton("##diagrunadv", ImVec2(bw, bh));
    else
      ImGui::Dummy(ImVec2(bw, bh));
    const bool hov = !st.diagAdvancedRunning && ImGui::IsItemHovered();
    dl->AddRect(bmn, bmx, hov ? sevColor : sevDim, 5 * scale);
    dl->AddText(ImVec2(bmn.x + (bw - ts.x) * 0.5f, bmn.y + (bh - ts.y) * 0.5f), sevColor,
               label.c_str());
    if (st.diagAdvancedRunning) {
      const float r = 6 * scale;
      const ImVec2 c(bmx.x + 10 * scale + r, bmn.y + bh * 0.5f);
      const float t0 = (float)ImGui::GetTime() * 6.0f;
      dl->PathClear();
      dl->PathArcTo(c, r, t0, t0 + 4.5f, 20);
      dl->PathStroke(sevColor, 0, 2.0f * scale);
    }
    ImGui::PopFont();
    if (clicked && ctx.actions.runDiagnostics) ctx.actions.runDiagnostics();
    y += bh;
  }

  if (st.diagHasAdvanced) {
    y += rowGap;
    ImGui::PushFont(th.fonts.sans12);
    for (int i = 0; i < advCount; i++) drawLine(advLines[i]);
    ImGui::PopFont();
    if (advHasIssue) drawTip();
  }

  ImGui::End();
  ImGui::PopStyleVar();
}

} // namespace ui
