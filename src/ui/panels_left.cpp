// Left glass rail — Textures tab (preset grid, custom texture, smoothing)
// and Import tab (model info, rotate controls, place-on-face).
#include <cstdio>

#include "ui/ui.h"
#include "ui/widgets.h"

namespace ui {

namespace {

// Tab bar styled like the mockup's pill switcher.
void tabBar(UiContext& ctx) {
  Theme& th = *ctx.theme;
  app::AppState& st = *ctx.state;
  float scale = th.scale;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  float width = ImGui::GetContentRegionAvail().x;
  float h = 34 * scale;
  ImVec2 mx(pos.x + width, pos.y + h);
  dl->AddRectFilled(pos, mx, col::rgba(0, 0, 0, 0.28f), 12 * scale);
  dl->AddRect(pos, mx, col::white(0.05f), 12 * scale, 0, 1.0f);

  const char* names[2] = {"Textures", "Import"};
  float tw = (width - 8 * scale) * 0.5f;
  ImGui::PushFont(th.fonts.sansSemi12);
  for (int i = 0; i < 2; i++) {
    ImVec2 tmn(pos.x + 4 * scale + i * (tw + 0 * scale), pos.y + 4 * scale);
    ImVec2 tmx(tmn.x + tw, pos.y + h - 4 * scale);
    ImGui::SetCursorScreenPos(tmn);
    ImGui::PushID(i);
    if (ImGui::InvisibleButton("##tab", ImVec2(tw, tmx.y - tmn.y)))
      st.leftTab = i;
    ImGui::PopID();
    ImVec2 ts = ImGui::CalcTextSize(names[i]);
    if (st.leftTab == i) {
      dl->AddRectFilled(ImVec2(tmn.x, tmn.y + 2), ImVec2(tmx.x, tmx.y + 4),
                        col::rgba(255, 45, 80, 0.2f), 9 * scale);
      roundedGradientV(dl, tmn, tmx, col::kAccentHi, col::kAccentLo, 9 * scale);
      dl->AddRect(tmn, tmx, col::white(0.15f), 9 * scale, 0, 1.0f);
      dl->AddText(ImVec2(tmn.x + (tw - ts.x) * 0.5f,
                         tmn.y + (tmx.y - tmn.y - ts.y) * 0.5f),
                  IM_COL32_WHITE, names[i]);
    } else {
      dl->AddText(ImVec2(tmn.x + (tw - ts.x) * 0.5f,
                         tmn.y + (tmx.y - tmn.y - ts.y) * 0.5f),
                  col::white(0.55f), names[i]);
    }
  }
  ImGui::PopFont();
  ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h));
  ImGui::Spacing();
}

void texturesTab(UiContext& ctx) {
  Theme& th = *ctx.theme;
  app::AppState& st = *ctx.state;
  float scale = th.scale;

  sectionHeader(th, "Height Maps");
  // 4-column preset grid
  float avail = ImGui::GetContentRegionAvail().x;
  float gapPx = 9 * scale;
  float tile = (avail - 3 * gapPx) / 4;
  for (int i = 0; i < app::kPresetTextureCount; i++) {
    if (i % 4 != 0) ImGui::SameLine(0, gapPx);
    char id[16];
    std::snprintf(id, sizeof(id), "tex%d", i);
    if (textureTile(id, ctx.presetThumbs[i], st.selectedPreset == i, tile,
                    app::kTexturePresets[i].name) &&
        ctx.actions.selectPreset)
      ctx.actions.selectPreset(i);
    if (i % 4 != 3 && i != app::kPresetTextureCount - 1) {
    } else {
      ImGui::Spacing();
    }
  }

  divider();
  sectionHeader(th, T(ctx, "ui.customMap").c_str());
  if (!st.customTextureName.empty()) {
    if (textureTile("customtex", ctx.customThumb, st.selectedPreset < 0, tile,
                    st.customTextureName.c_str()) &&
        ctx.actions.selectPreset)
      ctx.actions.selectPreset(-1);
    ImGui::SameLine(0, gapPx);
    ImGui::BeginGroup();
    ImGui::PushFont(th.fonts.sansMed12);
    ImGui::PushStyleColor(ImGuiCol_Text, col::white(0.82f));
    ImGui::TextWrapped("%s", st.customTextureName.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (ghostButton(th, T(ctx, "ui.removeCustomMap").c_str(), ImVec2(0, 26 * scale)) &&
        ctx.actions.removeCustomTexture)
      ctx.actions.removeCustomTexture();
    ImGui::EndGroup();
  } else {
    ImGui::PushFont(th.fonts.sans12);
    ImGui::PushStyleColor(ImGuiCol_Text, col::white(0.4f));
    ImGui::TextWrapped("Load your own grayscale height map (PNG/JPG).");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (ghostButton(th, T(ctx, "ui.uploadCustomMap").c_str(), ImVec2(0, 30 * scale)) &&
        ctx.actions.importCustomTexture)
      ctx.actions.importCustomTexture();
  }

  divider();
  sectionHeader(th, "Texture Smoothing");
  if (sliderRow(th, stripInfoIcon(T(ctx, "labels.textureSmoothing")).c_str(),
                &st.settings.textureSmoothing, 0, 20, "%.1f", false, 0.1) &&
      ctx.actions.settingsChanged)
    ctx.actions.settingsChanged();
}

void importTab(UiContext& ctx) {
  Theme& th = *ctx.theme;
  app::AppState& st = *ctx.state;
  float scale = th.scale;

  sectionHeader(th, "Model");
  if (ghostButton(th, T(ctx, "ui.loadStl").c_str(), ImVec2(0, 34 * scale)) &&
      ctx.actions.loadModel)
    ctx.actions.loadModel();
  ImGui::Spacing();

  // Mesh info card
  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float h = 64 * scale;
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + h), col::white(0.03f),
                      10 * scale);
    dl->AddRect(pos, ImVec2(pos.x + width, pos.y + h), col::white(0.05f),
                10 * scale, 0, 1.0f);
    ImGui::PushFont(th.fonts.sansMed12);
    dl->AddText(ImVec2(pos.x + 10 * scale, pos.y + 8 * scale),
                col::white(0.82f), st.meshName.c_str());
    ImGui::PopFont();
    char meta[128];
    const core::Bounds& b = st.meshBounds;
    std::snprintf(meta, sizeof(meta), "%zu tris\n%.1f x %.1f x %.1f mm",
                  st.meshTriangles, b.size.x, b.size.y, b.size.z);
    ImGui::PushFont(th.fonts.mono10);
    dl->AddText(ImVec2(pos.x + 10 * scale, pos.y + 26 * scale),
                col::white(0.35f), meta);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(width, h));
  }

  divider();
  sectionHeader(th, "Rotate Model");
  const char* axes[3] = {"X\xc2\xb0", "Y\xc2\xb0", "Z\xc2\xb0"};
  double* vals[3] = {&st.rotX, &st.rotY, &st.rotZ};
  float colW = (ImGui::GetContentRegionAvail().x - 16 * scale) / 3;
  ImGui::PushFont(th.fonts.mono11);
  for (int i = 0; i < 3; i++) {
    if (i) ImGui::SameLine(0, 8 * scale);
    ImGui::PushID(i);
    ImGui::SetNextItemWidth(colW);
    float f = (float)*vals[i];
    if (ImGui::DragFloat("##rot", &f, 1.0f, -360.0f, 360.0f, "%.0f"))
      *vals[i] = f;
    ImGui::PopID();
  }
  ImGui::PopFont();
  ImGui::Spacing();
  if (ghostButton(th, T(ctx, "ui.rotateApply").c_str(), ImVec2(0, 30 * scale)) &&
      ctx.actions.applyRotation) {
    ctx.actions.applyRotation(st.rotX, st.rotY, st.rotZ);
    st.rotX = st.rotY = st.rotZ = 0;
  }
  if (ghostButton(th, T(ctx, "ui.rotateReset").c_str(), ImVec2(0, 30 * scale)) &&
      ctx.actions.resetRotation)
    ctx.actions.resetRotation();
  if (toggleRow(th, "Rotation gizmo", &st.rotateGizmo) &&
      ctx.actions.toggleRotateGizmo)
    ctx.actions.toggleRotateGizmo(st.rotateGizmo);

  divider();
  sectionHeader(th, "Placement");
  const std::string placeLabel =
      st.placeOnFaceActive ? "Click a face..." : T(ctx, "ui.placeOnFace");
  if (ghostButton(th, placeLabel.c_str(), ImVec2(0, 30 * scale), true) &&
      ctx.actions.placeOnFace)
    ctx.actions.placeOnFace(!st.placeOnFaceActive);
}

} // namespace

void drawLeftPanel(UiContext& ctx) {
  const Layout& l = ctx.layout;
  Theme& th = *ctx.theme;
  float scale = th.scale;

  drawGlassRect(ImGui::GetBackgroundDrawList(), *ctx.glass, l.leftMin,
                l.leftMax, kPanelRounding * scale);

  ImGui::SetNextWindowPos(l.leftMin);
  ImGui::SetNextWindowSize(ImVec2(l.leftMax.x - l.leftMin.x,
                                  l.leftMax.y - l.leftMin.y));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(16 * scale, 16 * scale));
  ImGui::Begin("##leftpanel", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);

  tabBar(ctx);
  ImGui::Spacing();
  if (ctx.state->leftTab == 0)
    texturesTab(ctx);
  else
    importTab(ctx);

  ImGui::End();
  ImGui::PopStyleVar();
}

} // namespace ui
