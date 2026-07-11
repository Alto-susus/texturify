// Top toolbar: logo, New/Open/Save (project), Undo/Redo, status pill, Export.
#include <cctype>
#include <functional>

#include "ui/ui.h"
#include "ui/widgets.h"

namespace ui {

void drawToolbar(UiContext& ctx) {
  const Layout& l = ctx.layout;
  Theme& th = *ctx.theme;
  app::AppState& st = *ctx.state;
  float scale = th.scale;

  // Toolbar glass strip (square corners, bottom border like the mockup)
  ImDrawList* bg = ImGui::GetBackgroundDrawList();
  bg->AddImage(ctx.glass->blurTex(), l.toolbarMin, l.toolbarMax,
               ctx.glass->blurUv0(l.toolbarMin),
               ctx.glass->blurUv1(l.toolbarMax));
  bg->AddRectFilledMultiColor(l.toolbarMin, l.toolbarMax,
                              col::rgba(26, 26, 32, 0.78f),
                              col::rgba(26, 26, 32, 0.78f),
                              col::rgba(14, 14, 19, 0.5f),
                              col::rgba(14, 14, 19, 0.5f));
  bg->AddLine(ImVec2(l.toolbarMin.x, l.toolbarMax.y),
              ImVec2(l.toolbarMax.x, l.toolbarMax.y), col::white(0.07f));

  ImGui::SetNextWindowPos(l.toolbarMin);
  ImGui::SetNextWindowSize(ImVec2(l.toolbarMax.x - l.toolbarMin.x,
                                  l.toolbarMax.y - l.toolbarMin.y));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * scale, 0));
  ImGui::Begin("##toolbar", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollWithMouse);

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float barH = l.toolbarMax.y - l.toolbarMin.y;
  const float btnH = 34 * scale;
  float y = (barH - btnH) * 0.5f;

  // Custom window chrome: this toolbar doubles as the OS-native draggable
  // caption region (see app::CustomChrome / main.cpp), so every real control
  // in it must be registered here or a click on it would be swallowed as a
  // window-drag instead of a button press.
  ctx.dragExemptRects.clear();
  auto registerExempt = [&]() {
    ImVec2 mn = ImGui::GetItemRectMin();
    ImVec2 mx = ImGui::GetItemRectMax();
    ctx.dragExemptRects.push_back({mn.x, mn.y, mx.x, mx.y});
  };

  // Logo mark + wordmark
  {
    ImVec2 p(l.toolbarMin.x + 18 * scale, (barH - 26 * scale) * 0.5f);
    ImVec2 q(p.x + 26 * scale, p.y + 26 * scale);
    dl->AddRectFilled(ImVec2(p.x - 3, p.y + 3), ImVec2(q.x + 3, q.y + 6),
                      col::rgba(255, 45, 80, 0.25f), 12 * scale);
    if (ctx.logoTex) {
      dl->AddImageRounded(ctx.logoTex, p, q, ImVec2(0, 0), ImVec2(1, 1),
                          IM_COL32_WHITE, 8 * scale);
      dl->AddRect(p, q, col::white(0.2f), 8 * scale, 0, 1.0f);
    } else {
      // Fallback procedural chip if the icon asset failed to load.
      roundedGradientV(dl, p, q, col::kAccentHi, col::kAccentDeep, 8 * scale);
      dl->AddRectFilled(p, ImVec2(q.x, p.y + 8 * scale), col::white(0.25f),
                        8 * scale, ImDrawFlags_RoundCornersTop);
      dl->AddRect(p, q, col::white(0.2f), 8 * scale, 0, 1.0f);
    }
    ImGui::SetCursorPosX(26 * scale + 18 * scale + 10 * scale);
    ImGui::SetCursorPosY((barH - ImGui::GetFontSize() - 4) * 0.5f);
    ImGui::PushFont(th.fonts.sansSemi14);
    ImGui::TextUnformatted("Texturify");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::PushFont(th.fonts.sans12);
    ImGui::PushStyleColor(ImGuiCol_Text, col::white(0.4f));
    ImGui::TextUnformatted("Displace");
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }

  auto vdivider = [&]() {
    ImGui::SameLine(0, 14 * scale);
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddLine(ImVec2(p.x, (barH - 24 * scale) * 0.5f),
                ImVec2(p.x, (barH + 24 * scale) * 0.5f), col::white(0.09f));
    ImGui::Dummy(ImVec2(1, barH));
    ImGui::SameLine(0, 14 * scale);
  };

  vdivider();
  ImGui::SetCursorPosY(y);
  if (ghostButton(th, "New", ImVec2(0, btnH)) && ctx.actions.newProject)
    ctx.actions.newProject();
  registerExempt();
  ImGui::SameLine(0, 6 * scale);
  ImGui::SetCursorPosY(y);
  if (ghostButton(th, "Open", ImVec2(0, btnH)) && ctx.actions.openProject)
    ctx.actions.openProject();
  registerExempt();
  ImGui::SameLine(0, 6 * scale);
  ImGui::SetCursorPosY(y);
  if (ghostButton(th, "Save", ImVec2(0, btnH)) && ctx.actions.saveProject)
    ctx.actions.saveProject();
  registerExempt();

  vdivider();
  ImGui::SetCursorPosY(y);
  if (ghostButton(th, "Undo", ImVec2(0, btnH), ctx.canUndo) && ctx.actions.undo)
    ctx.actions.undo();
  registerExempt();
  ImGui::SameLine(0, 4 * scale);
  ImGui::SetCursorPosY(y);
  if (ghostButton(th, "Redo", ImVec2(0, btnH), ctx.canRedo) && ctx.actions.redo)
    ctx.actions.redo();
  registerExempt();

  vdivider();
  ImGui::SetCursorPosY(y);
  if (ghostButton(th, "Load model", ImVec2(0, btnH)) && ctx.actions.loadModel)
    ctx.actions.loadModel();
  registerExempt();

  vdivider();
  // Language picker: current code + a popup list of every loaded language,
  // shown by its own native name (mirrors main.js's <select> whose <option>s
  // are each language's own "lang.name" string).
  {
    ImGui::SetCursorPosY(y);
    std::string cur = ctx.i18n ? ctx.i18n->currentLanguage() : "en";
    for (char& c : cur) c = (char)std::toupper((unsigned char)c);
    if (ghostButton(th, cur.c_str(), ImVec2(44 * scale, btnH)))
      ImGui::OpenPopup("##langpicker");
    registerExempt();
    if (ImGui::BeginPopup("##langpicker")) {
      if (ctx.i18n) {
        for (const app::LanguageInfo& lang : ctx.i18n->available()) {
          const bool selected = lang.code == ctx.i18n->currentLanguage();
          if (ImGui::Selectable(lang.nativeName.c_str(), selected) && ctx.actions.setLanguage)
            ctx.actions.setLanguage(lang.code);
        }
      }
      ImGui::EndPopup();
    }
  }
  ImGui::SameLine(0, 6 * scale);
  // Help menu: What's New / License & Terms / Imprint & Privacy — main.js
  // places these as plain buttons at the bottom of the sidebar; here they're
  // grouped under one toolbar entry since this shell's right rail has no
  // footer real estate to spare.
  {
    ImGui::SetCursorPosY(y);
    if (ghostButton(th, "?", ImVec2(btnH, btnH))) ImGui::OpenPopup("##helpmenu");
    registerExempt();
    if (ImGui::BeginPopup("##helpmenu")) {
      if (ImGui::Selectable("What's New")) {
        st.welcomeOpen = true;
        st.welcomeAllowDismissPersist = false; // manual open never suppresses future auto-popups
        st.welcomeDontShowAgain = false;
      }
      // ctx.i18n->t() returns a temporary std::string; hold it in a local so
      // the c_str() passed to Selectable() stays valid for the call.
      std::string licenseLabel = ctx.i18n ? ctx.i18n->t("license.btn") : "License & Terms";
      if (ImGui::Selectable(licenseLabel.c_str())) st.licenseOpen = true;
      std::string imprintLabel = ctx.i18n ? ctx.i18n->t("imprint.btn") : "Imprint & Privacy";
      if (ImGui::Selectable(imprintLabel.c_str())) st.imprintOpen = true;
      ImGui::EndPopup();
    }
  }

  // Window-control cluster (minimize/maximize/close) — replaces the native
  // title bar's own buttons now that this toolbar is the OS-native draggable
  // caption region (app::CustomChrome). Drawn flush against the true window
  // edge, bypassing the toolbar window's own padding, same as modals.cpp's
  // drawCloseX. Hand-vectored icons (no font glyph dependency, same
  // rationale as the mesh-diagnostics popup's icons).
  const float winBtnW = 46 * scale;
  const float winCtrlW = winBtnW * 3;
  {
    auto winCtrlButton = [&](float xIndexFromRight, ImU32 hoverBg,
                             const std::function<void(ImDrawList*, ImVec2, float)>& drawIcon) {
      ImVec2 bmin(l.toolbarMax.x - winBtnW * (xIndexFromRight + 1), l.toolbarMin.y);
      ImVec2 bmax(bmin.x + winBtnW, l.toolbarMin.y + barH);
      ImGui::SetCursorScreenPos(bmin);
      ImGui::PushID((int)xIndexFromRight + 1000);
      const bool clicked = ImGui::InvisibleButton("##winctrl", ImVec2(winBtnW, barH));
      const bool hovered = ImGui::IsItemHovered();
      ImGui::PopID();
      registerExempt();
      if (hovered) dl->AddRectFilled(bmin, bmax, hoverBg);
      ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
      drawIcon(dl, center, scale);
      return clicked;
    };

    const ImU32 hoverNeutral = col::white(0.08f);
    const ImU32 hoverClose = col::rgba(232, 17, 35, 0.9f); // Windows' own close-hover red

    if (winCtrlButton(2, hoverNeutral, [](ImDrawList* d, ImVec2 c, float s) {
          d->AddLine(ImVec2(c.x - 5 * s, c.y), ImVec2(c.x + 5 * s, c.y),
                     col::white(0.85f), 1.2f * s);
        }) &&
        ctx.actions.minimizeWindow)
      ctx.actions.minimizeWindow();

    if (winCtrlButton(1, hoverNeutral, [&](ImDrawList* d, ImVec2 c, float s) {
          const ImU32 col = col::white(0.85f);
          if (ctx.windowMaximized) {
            d->AddRect(ImVec2(c.x - 4 * s, c.y - 2 * s), ImVec2(c.x + 4.5f * s, c.y + 4.5f * s),
                      col, 0, 0, 1.1f * s);
            d->AddRect(ImVec2(c.x - 4.5f * s, c.y - 4.5f * s), ImVec2(c.x + 4 * s, c.y + 2 * s),
                      col, 0, 0, 1.1f * s);
          } else {
            d->AddRect(ImVec2(c.x - 4.5f * s, c.y - 4.5f * s), ImVec2(c.x + 4.5f * s, c.y + 4.5f * s),
                      col, 0, 0, 1.1f * s);
          }
        }) &&
        ctx.actions.toggleMaximizeWindow)
      ctx.actions.toggleMaximizeWindow();

    if (winCtrlButton(0, hoverClose, [](ImDrawList* d, ImVec2 c, float s) {
          const ImU32 col = col::white(0.9f);
          d->AddLine(ImVec2(c.x - 4.5f * s, c.y - 4.5f * s), ImVec2(c.x + 4.5f * s, c.y + 4.5f * s),
                    col, 1.2f * s);
          d->AddLine(ImVec2(c.x - 4.5f * s, c.y + 4.5f * s), ImVec2(c.x + 4.5f * s, c.y - 4.5f * s),
                    col, 1.2f * s);
        }) &&
        ctx.actions.closeWindow)
      ctx.actions.closeWindow();
  }

  // Right side: status pill + Export
  {
    ImGui::PushFont(th.fonts.mono11);
    const char* status = st.meshDirty ? "Live" : "Baked";
    float pillW = ImGui::CalcTextSize(status).x + 38 * scale;
    ImGui::PopFont();
    float exportW = 96 * scale;
    float rightEdge = l.toolbarMax.x - 18 * scale - winCtrlW;
    ImGui::SameLine();
    ImGui::SetCursorPosX(rightEdge - exportW - 12 * scale - pillW - l.toolbarMin.x);
    ImGui::SetCursorPosY((barH - 30 * scale) * 0.5f);
    statusPill(th, status, st.meshDirty ? col::kAccent : col::kGreen);
    registerExempt();
    ImGui::SameLine(0, 12 * scale);
    ImGui::SetCursorPosY((barH - 36 * scale) * 0.5f);
    if (accentButton(th, "Export", ImVec2(exportW, 36 * scale)) &&
        ctx.actions.exportStl)
      ctx.actions.exportStl();
    registerExempt();
  }

  ImGui::End();
  ImGui::PopStyleVar();
}

} // namespace ui
