// Welcome / License / Imprint modal overlays — port of main.js's
// #welcome-overlay / #license-overlay / #imprint-overlay (main.js:1180-1229,
// wireEvents() ~1526-1541; markup/CSS: index.html "*-overlay"/"*-modal"
// elements, style.css ~.license-overlay/.welcome-overlay). ImGui's
// BeginPopupModal already gives the fixed, centered, dimmed-backdrop look
// main.js's CSS achieves with `position:fixed` + a flex-centered backdrop
// div, so no separate overlay window is needed here.
//
// Deviation: main.js also closes on backdrop click; these use a true modal
// (BeginPopupModal blocks interaction with the rest of the app while open)
// and are only dismissed via the × button, "Close"/"Got it", or Escape — a
// deliberate simplification, since backdrop-click-to-dismiss adds real
// complexity for marginal benefit in a native app where modals are already
// an expected, deliberate interaction.
//
// License/imprint bodies are real legal text (i18n keys `license.*`/
// `imprint.*`), so they're looked up via app::I18n::t() and localized. The
// source strings carry inline HTML (`<strong>`, `<a href>`, `<br>`, `<em>`)
// for the web page; stripHtml() below reduces that to plain wrapped text
// (dropping bold/italic emphasis, keeping link text — and appending the URL
// when the visible text doesn't already look like one) since ImGui has no
// inline rich-text renderer. The welcome modal's content has no data-i18n at
// all even in the original (hardcoded English), and its "new in this
// release" changelog is native-port-specific rather than a literal port of
// the web app's own changelog, since the two ship different feature sets.
#include <cfloat>
#include <cstring>

#include "ui/ui.h"
#include "ui/widgets.h"

namespace ui {

namespace {

// Shared modal chrome (dark surface, rounded corners already global via
// Theme::init's PopupRounding, dimmed backdrop). Call between this and
// endModal() only while it returns true; `open` is BeginPopupModal's p_open.
bool beginModal(Theme& th, const char* id, bool* open, float maxWidth, bool scrolls) {
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                          ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  const float w = maxWidth * th.scale;
  const float maxH = scrolls ? io.DisplaySize.y * 0.85f : FLT_MAX;
  ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, maxH));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.086f, 0.09f, 0.102f, 0.98f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(col::white(0.10f)));
  ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.6f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28 * th.scale, 24 * th.scale));
  ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
  const bool visible = ImGui::BeginPopupModal(
      id, open,
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollWithMouse);
  if (!visible) {
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
  }
  return visible;
}

void endModal() {
  ImGui::EndPopup();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(3);
}

// Small circular × in the modal's top-right corner (main.js's
// .license-close-btn). Must be called right after beginModal() succeeds.
bool drawCloseX(Theme& th) {
  const ImVec2 wp = ImGui::GetWindowPos();
  const ImVec2 ws = ImGui::GetWindowSize();
  const float bs = 22 * th.scale;
  const ImVec2 bmn(wp.x + ws.x - 14 * th.scale - bs, wp.y + 12 * th.scale);
  ImGui::SetCursorScreenPos(bmn);
  ImGui::PushID("##modalclose");
  const bool clicked = ImGui::InvisibleButton("##close", ImVec2(bs, bs));
  const bool hov = ImGui::IsItemHovered();
  ImGui::PopID();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 c(bmn.x + bs * 0.5f, bmn.y + bs * 0.5f);
  dl->AddCircleFilled(c, bs * 0.5f, hov ? col::white(0.16f) : col::white(0.07f));
  ImGui::PushFont(th.fonts.sansSemi12);
  const char* x = "\xc3\x97"; // ×
  ImVec2 ts = ImGui::CalcTextSize(x);
  dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), col::white(0.8f), x);
  ImGui::PopFont();
  return clicked;
}

void modalHeading(Theme& th, const char* text) {
  ImGui::Spacing();
  ImGui::PushFont(th.fonts.sansSemi12);
  ImGui::PushStyleColor(ImGuiCol_Text, col::kAccentText);
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
  ImGui::PopFont();
}

void modalBody(Theme& th, const std::string& text, const char* bullet = nullptr) {
  ImGui::PushFont(th.fonts.sans12);
  ImGui::PushStyleColor(ImGuiCol_Text, col::white(0.68f));
  if (bullet)
    ImGui::TextWrapped("%s %s", bullet, text.c_str());
  else
    ImGui::TextWrapped("%s", text.c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Spacing();
}

// Right-aligned close/confirm button (main.js's align-self:flex-end).
bool modalCloseButton(Theme& th, const char* label) {
  const float w = 96 * th.scale, h = 32 * th.scale;
  const float avail = ImGui::GetContentRegionAvail().x;
  if (avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - w);
  return accentButton(th, label, ImVec2(w, h));
}

void drawWelcomeModal(UiContext& ctx) {
  app::AppState& st = *ctx.state;
  Theme& th = *ctx.theme;
  const char* kId = "Welcome to Texturify##welcome";
  if (st.welcomeOpen) ImGui::OpenPopup(kId);
  if (!beginModal(th, kId, &st.welcomeOpen, 480, true)) return;

  const bool closedByX = drawCloseX(th);

  ImGui::PushFont(th.fonts.sansSemi14);
  ImGui::TextUnformatted(T(ctx, "welcome.title").c_str());
  ImGui::PopFont();
  modalBody(th, T(ctx, "welcome.intro"));
  modalBody(th, T(ctx, "welcome.inspiredBy"));

  modalHeading(th, T(ctx, "welcome.quickStart").c_str());
  modalBody(th, T(ctx, "welcome.step1"), "1.");
  modalBody(th, T(ctx, "welcome.step2"), "2.");
  modalBody(th, T(ctx, "welcome.step3"), "3.");
  modalBody(th, T(ctx, "welcome.step4"), "4.");

  modalHeading(th, T(ctx, "welcome.nativePort").c_str());
  modalBody(th, T(ctx, "welcome.bullet1"), "-");
  modalBody(th, T(ctx, "welcome.bullet2"), "-");
  modalBody(th, T(ctx, "welcome.bullet3"), "-");
  modalBody(th, T(ctx, "welcome.bullet4"), "-");

  ImGui::Spacing();
  ImGui::PushFont(th.fonts.sans12);
  const std::string dontShowAgain = T(ctx, "welcome.dontShowAgain");
  ImGui::Checkbox(dontShowAgain.c_str(), &st.welcomeDontShowAgain);
  ImGui::PopFont();
  ImGui::Spacing();
  const std::string gotItLabel = T(ctx, "welcome.gotIt");
  const bool gotIt = modalCloseButton(th, gotItLabel.c_str());

  const bool wantClose = closedByX || gotIt || ImGui::IsKeyPressed(ImGuiKey_Escape);
  if (wantClose) {
    if (ctx.actions.closeWelcome) ctx.actions.closeWelcome(st.welcomeDontShowAgain);
    st.welcomeOpen = false;
    ImGui::CloseCurrentPopup();
  }
  endModal();
}

void drawLicenseModal(UiContext& ctx) {
  app::AppState& st = *ctx.state;
  Theme& th = *ctx.theme;
  app::I18n& i18n = *ctx.i18n;
  const char* kId = "License##license";
  if (st.licenseOpen) ImGui::OpenPopup(kId);
  if (!beginModal(th, kId, &st.licenseOpen, 460, true)) return;

  const bool closedByX = drawCloseX(th);
  ImGui::PushFont(th.fonts.sansSemi14);
  ImGui::TextUnformatted(i18n.t("license.title").c_str());
  ImGui::PopFont();
  ImGui::Spacing();

  // Display order matches index.html's <li> order, which isn't item1..8
  // sequential (item3 — the sponsor-links item — is deliberately placed
  // second-to-last in the original markup).
  static constexpr int kOrder[] = {1, 2, 4, 5, 6, 7, 3, 8};
  for (int idx : kOrder)
    modalBody(th, stripHtml(i18n.t("license.item" + std::to_string(idx))), "-");

  ImGui::Spacing();
  const bool closeBtn = modalCloseButton(th, "Close");
  if (closedByX || closeBtn || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    st.licenseOpen = false;
    ImGui::CloseCurrentPopup();
  }
  endModal();
}

void drawImprintModal(UiContext& ctx) {
  app::AppState& st = *ctx.state;
  Theme& th = *ctx.theme;
  app::I18n& i18n = *ctx.i18n;
  const char* kId = "Imprint##imprint";
  if (st.imprintOpen) ImGui::OpenPopup(kId);
  if (!beginModal(th, kId, &st.imprintOpen, 520, true)) return;

  const bool closedByX = drawCloseX(th);
  ImGui::PushFont(th.fonts.sansSemi14);
  ImGui::TextUnformatted(i18n.t("imprint.title").c_str());
  ImGui::PopFont();

  modalHeading(th, i18n.t("imprint.sectionImprint").c_str());
  modalBody(th, stripHtml(i18n.t("imprint.info")));
  modalBody(th, stripHtml(i18n.t("imprint.contact")));
  modalBody(th, stripHtml(i18n.t("imprint.odr")));

  modalHeading(th, i18n.t("imprint.sectionPrivacy").c_str());
  modalBody(th, stripHtml(i18n.t("imprint.privacyIntro")));
  for (const char* key : {"imprint.privacyHosting", "imprint.privacyLocal",
                          "imprint.privacyNoCookies", "imprint.privacyExternal",
                          "imprint.privacyRights"})
    modalBody(th, stripHtml(i18n.t(key)), "-");

  ImGui::Spacing();
  const bool closeBtn = modalCloseButton(th, "Close");
  if (closedByX || closeBtn || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    st.imprintOpen = false;
    ImGui::CloseCurrentPopup();
  }
  endModal();
}

} // namespace

void drawModals(UiContext& ctx) {
  drawWelcomeModal(ctx);
  drawLicenseModal(ctx);
  drawImprintModal(ctx);
}

} // namespace ui
