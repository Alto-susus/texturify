// Floating inset panel (top-left of the viewport, cylindrical mode only):
// a top-down X-Y silhouette of the part with a draggable center dot + radius
// ring, driving settings.cylinderCenterX/Y/cylinderRadius. Port of main.js's
// _redrawCylinderPanel/_cylinderPointerDown/Move/Up/_cylinderWheel — the
// silhouette rasterization itself lives in app/cylinder_silhouette.{h,cpp}
// and is rebuilt by main.cpp (owns the GL texture) into ctx.cylinderSilhouette.
#include <algorithm>
#include <cmath>

#include "ui/ui.h"
#include "ui/widgets.h"

namespace ui {

namespace {

// Raw silhouette-texture resolution (matches app::buildCylinderSilhouette's
// W/H argument in main.cpp) — the panel's world<->pixel transform operates
// in this fixed space regardless of display scale, mirroring the JS canvas's
// width=240/height=240 attributes vs. its (possibly different) CSS size.
constexpr int kCanvasPx = 240;
constexpr float kCanvasLogicalSize = 240.0f; // * scale for on-screen size

enum class DragMode { None, Center, Radius, Pan };

// Persistent view state — one floating panel instance, so plain statics are
// fine (mirrors main.js's module-level _cyl* globals).
struct CylState {
  bool initialized = false;
  int builtEpoch = -1;
  double viewCxw = 0, viewCyw = 0, viewScale = 1; // panned view transform
  DragMode drag = DragMode::None;
  DragMode hover = DragMode::None;
  double panLastPx = 0, panLastPy = 0;
};
CylState g_cyl;

double effectiveCenterX(const app::Settings& s, const core::Bounds& b) {
  return s.cylinderCenterX.value_or(b.center.x);
}
double effectiveCenterY(const app::Settings& s, const core::Bounds& b) {
  return s.cylinderCenterY.value_or(b.center.y);
}
double effectiveRadius(const app::Settings& s, const core::Bounds& b) {
  if (s.cylinderRadius) return *s.cylinderRadius;
  return std::max(std::max(b.size.x, b.size.y) * 0.5, 1e-6);
}

constexpr double kCenterHitPx = 10, kRingHitPx = 8;

DragMode handleAt(double px, double py, const CylState& v,
                  const app::Settings& s, const core::Bounds& b) {
  const double cpx = (effectiveCenterX(s, b) - v.viewCxw) * v.viewScale + kCanvasPx / 2.0;
  const double cpy = kCanvasPx / 2.0 - (effectiveCenterY(s, b) - v.viewCyw) * v.viewScale;
  const double dx = px - cpx, dy = py - cpy;
  const double dist = std::sqrt(dx * dx + dy * dy);
  const double ringPx = effectiveRadius(s, b) * v.viewScale;
  if (dist <= kCenterHitPx) return DragMode::Center;
  if (std::abs(dist - ringPx) <= kRingHitPx) return DragMode::Radius;
  return DragMode::None;
}

} // namespace

void drawCylinderPanel(UiContext& ctx) {
  app::AppState& st = *ctx.state;
  app::Settings& s = st.settings;
  if (s.mappingMode != 3 /* MODE_CYLINDRICAL */) return;

  Theme& th = *ctx.theme;
  const Layout& l = ctx.layout;
  const float scale = th.scale;
  const CylinderSilhouetteView& sil = ctx.cylinderSilhouette;

  // A fresh silhouette build (geometry topology changed) resets the pan/zoom
  // view back to its build-time anchor, matching _buildCylinderSilhouette's
  // unconditional `_cylPanelTransform = { scale, cxw, cyw, W, H }`.
  if (sil.valid && sil.geometryEpoch != g_cyl.builtEpoch) {
    g_cyl.builtEpoch = sil.geometryEpoch;
    g_cyl.viewCxw = sil.cxw;
    g_cyl.viewCyw = sil.cyw;
    g_cyl.viewScale = sil.scale;
    g_cyl.initialized = true;
  }

  const float inset = 14 * scale;
  const float panelW = kCanvasLogicalSize * scale;
  const float canvasH = kCanvasLogicalSize * scale;
  const float labelH = 30 * scale;
  const bool minimized = s.cylinderPanelMinimized;
  const float panelH = minimized ? labelH : canvasH + labelH;
  // Sits directly below the top-left "Displacement · Live/Baked" status pill
  // (viewport_ui.cpp: inset + 32*scale tall) with a small gap.
  const ImVec2 mn(l.vpMin.x + inset, l.vpMin.y + inset + 32 * scale + 10 * scale);
  const ImVec2 mx(mn.x + panelW, mn.y + panelH);

  ImGui::SetNextWindowPos(mn);
  ImGui::SetNextWindowSize(ImVec2(panelW, panelH));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("##cylinderpanel", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImDrawList* dl = ImGui::GetWindowDrawList();

  dl->AddRectFilled(mn, mx, col::rgba(14, 20, 24, 0.94f), 10 * scale);
  dl->AddRect(mn, mx, col::white(0.08f), 10 * scale, 0, 1.0f);

  if (!minimized) {
    const ImVec2 cmn = mn, cmx(mn.x + canvasH, mn.y + canvasH); // square canvas
    dl->PushClipRect(cmn, cmx, true);
    dl->AddRectFilled(cmn, cmx, col::rgba(14, 20, 24, 1.0f), 10 * scale,
                      ImDrawFlags_RoundCornersTop);

    if (!sil.valid) {
      ImGui::PushFont(th.fonts.mono10);
      const std::string msg = T(ctx, "ui.cylinderNoModel1");
      const std::string msg2 = T(ctx, "ui.cylinderNoModel2");
      ImVec2 t1 = ImGui::CalcTextSize(msg.c_str()), t2 = ImGui::CalcTextSize(msg2.c_str());
      ImVec2 c((cmn.x + cmx.x) * 0.5f, (cmn.y + cmx.y) * 0.5f);
      dl->AddText(ImVec2(c.x - t1.x * 0.5f, c.y - t1.y - 2), col::white(0.45f), msg.c_str());
      dl->AddText(ImVec2(c.x - t2.x * 0.5f, c.y + 4), col::white(0.45f), msg2.c_str());
      ImGui::PopFont();
    } else {
      // Translate the pre-rasterized silhouette by anchor-vs-view delta so
      // panning shifts it without re-rasterizing (matches _redrawCylinderPanel).
      const double dxPx = (sil.cxw - g_cyl.viewCxw) * g_cyl.viewScale;
      const double dyPx = -(sil.cyw - g_cyl.viewCyw) * g_cyl.viewScale;
      const ImVec2 imn(cmn.x + (float)(dxPx * scale), cmn.y + (float)(dyPx * scale));
      dl->AddImage(sil.tex, imn, ImVec2(imn.x + canvasH, imn.y + canvasH));

      const double cx = effectiveCenterX(s, st.meshBounds);
      const double cy = effectiveCenterY(s, st.meshBounds);
      const double r = effectiveRadius(s, st.meshBounds);
      const float px = cmn.x + (float)((cx - g_cyl.viewCxw) * g_cyl.viewScale * scale) +
                       canvasH * 0.5f;
      const float py = cmn.y + canvasH * 0.5f -
                       (float)((cy - g_cyl.viewCyw) * g_cyl.viewScale * scale);
      const float pr = std::max(2.0f, (float)(r * g_cyl.viewScale * scale));

      const DragMode active = g_cyl.drag != DragMode::None ? g_cyl.drag : g_cyl.hover;
      const bool ringActive = active == DragMode::Radius;
      const bool centerActive = active == DragMode::Center;

      dl->AddCircle(ImVec2(px, py), pr, ringActive ? IM_COL32(0x7b, 0xe0, 0xe0, 255)
                                                   : IM_COL32(0x22, 0xa3, 0xa3, 255),
                   64, ringActive ? 3.5f * scale : 2.0f * scale);
      if (ringActive) {
        dl->AddCircle(ImVec2(px, py), pr - 5 * scale, col::rgba(123, 224, 224, 0.6f), 64, 1.0f);
        dl->AddCircle(ImVec2(px, py), pr + 5 * scale, col::rgba(123, 224, 224, 0.6f), 64, 1.0f);
      }
      const float dotR = (centerActive ? 8.0f : 6.0f) * scale;
      dl->AddCircleFilled(ImVec2(px, py), dotR, centerActive ? IM_COL32(0x7b, 0xe0, 0xe0, 255)
                                                             : IM_COL32(0x22, 0xa3, 0xa3, 255));
      dl->AddCircle(ImVec2(px, py), dotR, IM_COL32_WHITE, 0, 1.5f * scale);

      dl->AddLine(ImVec2(px - 12 * scale, py), ImVec2(px - 8 * scale, py), col::white(0.55f));
      dl->AddLine(ImVec2(px + 8 * scale, py), ImVec2(px + 12 * scale, py), col::white(0.55f));
      dl->AddLine(ImVec2(px, py - 12 * scale), ImVec2(px, py - 8 * scale), col::white(0.55f));
      dl->AddLine(ImVec2(px, py + 8 * scale), ImVec2(px, py + 12 * scale), col::white(0.55f));
    }
    dl->PopClipRect();

    // Interaction surface — invisible button over the whole canvas.
    ImGui::SetCursorScreenPos(cmn);
    ImGui::InvisibleButton("##cylcanvas", ImVec2(canvasH, canvasH));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();
    const double mpx = (io.MousePos.x - cmn.x) / scale;
    const double mpy = (io.MousePos.y - cmn.y) / scale;
    const double wx = (mpx - kCanvasPx / 2.0) / g_cyl.viewScale + g_cyl.viewCxw;
    const double worldY = (kCanvasPx / 2.0 - mpy) / g_cyl.viewScale + g_cyl.viewCyw;

    bool changed = false;
    if (sil.valid) {
      if (ImGui::IsItemActivated()) {
        const bool panButton = ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                               ImGui::IsMouseDown(ImGuiMouseButton_Right);
        DragMode handle = panButton ? DragMode::None : handleAt(mpx, mpy, g_cyl, s, st.meshBounds);
        if (handle != DragMode::None) {
          g_cyl.drag = handle;
        } else {
          g_cyl.drag = DragMode::Pan;
          g_cyl.panLastPx = mpx;
          g_cyl.panLastPy = mpy;
        }
      }
      if (active && g_cyl.drag != DragMode::None) {
        if (g_cyl.drag == DragMode::Center) {
          s.cylinderCenterX = wx;
          s.cylinderCenterY = worldY;
          changed = true;
        } else if (g_cyl.drag == DragMode::Radius) {
          const double ccx = effectiveCenterX(s, st.meshBounds);
          const double ccy = effectiveCenterY(s, st.meshBounds);
          const double dx = wx - ccx, dy = worldY - ccy;
          s.cylinderRadius = std::max(0.1, std::sqrt(dx * dx + dy * dy));
          changed = true;
        } else if (g_cyl.drag == DragMode::Pan) {
          const double dPx = mpx - g_cyl.panLastPx, dPy = mpy - g_cyl.panLastPy;
          g_cyl.viewCxw -= dPx / g_cyl.viewScale;
          g_cyl.viewCyw += dPy / g_cyl.viewScale;
          g_cyl.panLastPx = mpx;
          g_cyl.panLastPy = mpy;
        }
      }
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
          !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
          !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        g_cyl.drag = DragMode::None;

      g_cyl.hover = (g_cyl.drag == DragMode::None && hovered)
                       ? handleAt(mpx, mpy, g_cyl, s, st.meshBounds)
                       : g_cyl.hover;
      if (g_cyl.drag == DragMode::None && !hovered) g_cyl.hover = DragMode::None;

      // Wheel zoom on the radius ring only (matches _cylinderWheel's active zone).
      if (hovered && g_cyl.drag == DragMode::None && io.MouseWheel != 0) {
        const double ccx = effectiveCenterX(s, st.meshBounds);
        const double ccy = effectiveCenterY(s, st.meshBounds);
        const double cpx = (ccx - g_cyl.viewCxw) * g_cyl.viewScale + kCanvasPx / 2.0;
        const double cpy = kCanvasPx / 2.0 - (ccy - g_cyl.viewCyw) * g_cyl.viewScale;
        const double dist = std::sqrt((mpx - cpx) * (mpx - cpx) + (mpy - cpy) * (mpy - cpy));
        const double ringPx = effectiveRadius(s, st.meshBounds) * g_cyl.viewScale;
        if (dist <= ringPx + kRingHitPx) {
          // JS: factor = 0.95^(deltaY/100); one ImGui wheel "tick" (1.0) is
          // one JS wheel notch (~100 deltaY), scroll-up (+MouseWheel) growing
          // the radius to match scroll-up (-deltaY) in the browser.
          const double factor = std::pow(0.95, -(double)io.MouseWheel);
          s.cylinderRadius = std::max(0.1, effectiveRadius(s, st.meshBounds) * factor);
          changed = true;
        }
      }
    }
    if (changed && ctx.actions.settingsChanged) ctx.actions.settingsChanged();
  }

  // Label row + minimize toggle.
  const ImVec2 lmn(mn.x, mn.y + (minimized ? 0 : canvasH));
  ImGui::PushFont(th.fonts.mono10);
  ImGui::PushStyleColor(ImGuiCol_Text, col::white(0.55f));
  const std::string panelLabel = T(ctx, "ui.cylinderPanelLabel");
  dl->AddText(ImVec2(lmn.x + 8 * scale, lmn.y + (labelH - ImGui::GetFontSize()) * 0.5f),
             col::white(0.55f), panelLabel.c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();

  const float btnSize = 18 * scale;
  const ImVec2 bmn(mx.x - 8 * scale - btnSize, lmn.y + (labelH - btnSize) * 0.5f);
  const ImVec2 bmx(bmn.x + btnSize, bmn.y + btnSize);
  ImGui::SetCursorScreenPos(bmn);
  ImGui::InvisibleButton("##cylminimize", ImVec2(btnSize, btnSize));
  const bool btnHover = ImGui::IsItemHovered();
  dl->AddRect(bmn, bmx, btnHover ? col::kAccent : col::white(0.15f), 3 * scale);
  const ImVec2 c((bmn.x + bmx.x) * 0.5f, (bmn.y + bmx.y) * 0.5f);
  if (minimized) {
    dl->AddLine(ImVec2(c.x, c.y - 3 * scale), ImVec2(c.x, c.y + 3 * scale), col::white(0.7f), 1.5f);
    dl->AddLine(ImVec2(c.x - 3 * scale, c.y), ImVec2(c.x + 3 * scale, c.y), col::white(0.7f), 1.5f);
  } else {
    dl->AddLine(ImVec2(c.x - 3 * scale, c.y), ImVec2(c.x + 3 * scale, c.y), col::white(0.7f), 1.5f);
  }
  if (ImGui::IsItemClicked()) s.cylinderPanelMinimized = !s.cylinderPanelMinimized;

  ImGui::End();
  ImGui::PopStyleVar();
}

} // namespace ui
