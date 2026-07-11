// Spectre Displace 1a shell: fixed docked layout (58px toolbar, 290px left
// rail, flexible viewport, 340px right rail, 14px gutters) with glass panels.
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "app/app_state.h"
#include "app/custom_chrome.h"
#include "app/i18n.h"
#include "app/preset_textures.h"
#include "ui/glass.h"
#include "ui/theme.h"

namespace ui {

struct Layout {
  ImVec2 windowSize;
  ImVec2 toolbarMin, toolbarMax;
  ImVec2 leftMin, leftMax;
  ImVec2 vpMin, vpMax;
  ImVec2 rightMin, rightMax;
};

Layout computeLayout(float w, float h, float scale);

// Host callbacks; unset functions render their control disabled.
struct UiActions {
  std::function<void(int)> selectPreset; // preset index
  std::function<void()> importCustomTexture;
  std::function<void()> removeCustomTexture;
  std::function<void()> loadModel;
  std::function<void()> newProject;
  std::function<void()> openProject;
  std::function<void()> saveProject;
  std::function<void()> undo;
  std::function<void()> redo;
  std::function<void()> exportStl;
  std::function<void()> export3mf;
  std::function<void()> bake;
  std::function<void()> settingsChanged; // any settings edit → refresh preview
  // bottomAngleLimit / topAngleLimit / boundaryFalloff edits — these change
  // the CPU-computed boundary falloff attribute (main.js _falloffDirty),
  // which is a separate, costlier recompute than the live shader uniforms
  // settingsChanged covers. Fired in addition to settingsChanged.
  std::function<void()> maskSettingsChanged;
  std::function<void()> viewChanged;     // wireframe/projection/preview toggles
  std::function<void()> smartResolution;
  std::function<void()> resetSettings;
  std::function<void(double, double, double)> applyRotation; // X/Y/Z degrees
  std::function<void()> resetRotation;
  std::function<void(bool)> toggleRotateGizmo;
  std::function<void(bool)> placeOnFace; // togglePlaceOnFace(active)
  std::function<void()> clearExclusions;
  // Advanced mesh diagnostics ("Run Advanced Checks") — fast checks run
  // automatically and need no action. Cancellable; no-op while already
  // running (see app::DiagnosticsRunner).
  std::function<void()> runDiagnostics;
  std::function<void()> dismissDiagnostics; // popup's × button
  std::function<void(app::DiagHighlight)> toggleDiagHighlight;
  std::function<void(bool)> togglePrecisionMasking;
  std::function<void()> refreshPrecisionMesh;
  // 3D displacement preview toggle — subdivides the mesh for real vertex
  // displacement (see ModelSession::setDisplacementPreview). May revert
  // st.displacementPreview3D back to false if there's no model/texture yet.
  std::function<void(bool)> toggleDisplacementPreview;
  // Cylinder-axis panel (cylindrical projection mode only)
  std::function<void()> cylinderAutofit;
  std::function<void()> cylinderReset;

  // UI language (toolbar picker) — rebuilds the font atlas for the new
  // language's glyph ranges (see ui::Theme::init) and persists the choice.
  std::function<void(const std::string&)> setLanguage;
  // Welcome modal close: `dontShowAgain` is the checkbox state at close
  // time; only actually persisted when state.welcomeAllowDismissPersist is
  // set (main.js's openWelcome({allowDismissPersist})).
  std::function<void(bool)> closeWelcome;

  // Custom window chrome (replaces the native title bar — see
  // app::CustomChrome). No main.js equivalent; a web page has no window frame.
  std::function<void()> minimizeWindow;
  std::function<void()> toggleMaximizeWindow;
  std::function<void()> closeWindow;
};

// Cached silhouette rasterization for the cylinder-axis panel; owned and
// rebuilt by main.cpp (it needs GL texture upload + ModelSession geometry
// access, both outside ui/'s dependencies) whenever
// ModelSession::geometryEpoch() changes. cxw/cyw/scale are the build-time
// world<->pixel anchor (app::CylinderSilhouette's fields) — the panel's
// *view* transform starts equal to this anchor and is mutated by panning,
// entirely within ui/cylinder_panel.cpp's own static state.
struct CylinderSilhouetteView {
  bool valid = false;
  ImTextureID tex = (ImTextureID)0;
  int width = 0, height = 0;
  double cxw = 0, cyw = 0, scale = 1;
  int geometryEpoch = -1; // last epoch this texture was built from
};

struct UiContext {
  app::AppState* state = nullptr;
  Theme* theme = nullptr;
  GlassCompositor* glass = nullptr;
  app::I18n* i18n = nullptr;
  UiActions actions;
  Layout layout;

  // Texture thumbnails (GL textures wrapped as ImTextureID; 0 = not loaded)
  ImTextureID presetThumbs[app::kPresetTextureCount] = {};
  ImTextureID customThumb = (ImTextureID)0;
  // Toolbar logo mark (assets/icon/logo_64.png); 0 = not loaded, toolbar.cpp
  // falls back to the original procedural gradient chip in that case.
  ImTextureID logoTex = (ImTextureID)0;

  bool canUndo = false, canRedo = false;

  CylinderSilhouetteView cylinderSilhouette;

  // Custom window chrome: whether the window is currently maximized (picks
  // the restore- vs. maximize-icon in the toolbar's window-control cluster),
  // and the screen-space rects of the toolbar's own interactive controls —
  // drawToolbar() clears and repopulates this every frame; main.cpp feeds it
  // straight into app::CustomChrome::updateHitTestRegion() right after, so
  // clicks on real buttons aren't swallowed as a window-drag.
  bool windowMaximized = false;
  std::vector<app::ChromeRect> dragExemptRects;
};

// Shorthand for ctx.i18n->t(key). Every ui/*.cpp draw function receives a
// fully-populated UiContext (i18n is always set by main.cpp, same as
// ctx.theme/ctx.glass — none of those are null-checked at call sites
// either), so this assumes ctx.i18n is non-null.
//
// Safe to use directly as a function argument (`widget(T(ctx, "key").c_str(),
// ...)`)  — the returned std::string is a temporary that lives until the end
// of the full expression, i.e. through the whole call it's used in. It is
// NOT safe to store the .c_str() result in a variable for use in a later
// statement (the temporary is destroyed at the end of ITS OWN statement).
inline std::string T(UiContext& ctx, const char* key) { return ctx.i18n->t(key); }
inline std::string T(UiContext& ctx, const char* key,
                     std::initializer_list<std::pair<const char*, std::string>> params) {
  return ctx.i18n->t(key, params);
}

// The original web app appends " ⓘ" (U+24D8) to labels that show a tooltip
// on hover; that glyph isn't in the loaded font's range and hover-tooltips
// aren't wired up on these widgets, so plain-label call sites strip it.
std::string stripInfoIcon(std::string s);
// Same idea for the leading "⚠ " (U+26A0) on warnings.* strings — banners
// already convey severity via color, so the glyph (also outside the loaded
// font's range) is dropped rather than rendered as tofu.
std::string stripWarningIcon(std::string s);
// Same idea for the leading "✔ " (U+2714) on diag.meshOk/diag.advancedOk.
std::string stripCheckIcon(std::string s);
// Reduces the small set of HTML constructs used in a few i18n values
// (license.*/imprint.*/diag.recommendFix) to plain text: <br> -> newline,
// <a href="URL">TEXT</a> -> TEXT (plus " (URL)" when TEXT doesn't already
// look like the destination), all other tags (<strong>, <em>, ...) dropped,
// keeping inner text. ImGui has no inline rich-text renderer.
std::string stripHtml(const std::string& in);

// Draw the whole frame's UI (call inside an ImGui frame, after
// GlassCompositor::composite()). Individual pieces live in toolbar.cpp,
// panels_left.cpp, panels_right.cpp, viewport_ui.cpp.
void drawUi(UiContext& ctx);

void drawToolbar(UiContext& ctx);
void drawLeftPanel(UiContext& ctx);
void drawRightPanel(UiContext& ctx);
void drawViewportHud(UiContext& ctx);
void drawCylinderPanel(UiContext& ctx);
void drawDiagnosticsPanel(UiContext& ctx);
void drawModals(UiContext& ctx); // Welcome / License / Imprint

} // namespace ui
