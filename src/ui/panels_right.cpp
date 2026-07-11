// Right glass rail — every parameter of the original app, Spectre-styled:
// projection, UV transform, displacement, masking/exclusions, resolution &
// quality (incl. regularize advanced), diagnostics, export/bake footer.
#include <algorithm>
#include <cstdio>

#include "ui/ui.h"
#include "ui/widgets.h"

namespace ui {

namespace {

void projectionSection(UiContext& ctx) {
  Theme& th = *ctx.theme;
  app::Settings& s = ctx.state->settings;
  bool changed = false;

  sectionHeader(th, T(ctx, "sections.projection").c_str(), true, true);
  // Display order mirrors the original select; values = mapping mode ids.
  static const int kOrder[7] = {5, 6, 3, 4, 0, 1, 2};
  const std::string labels[7] = {
      T(ctx, "projection.triplanar"),   T(ctx, "projection.cubic"),
      T(ctx, "projection.cylindrical"), T(ctx, "projection.spherical"),
      T(ctx, "projection.planarXY"),    T(ctx, "projection.planarXZ"),
      T(ctx, "projection.planarYZ")};
  const char* kOrderedLabels[7];
  for (int i = 0; i < 7; i++) kOrderedLabels[i] = labels[i].c_str();
  int sel = 0;
  for (int i = 0; i < 7; i++)
    if (kOrder[i] == s.mappingMode) sel = i;
  if (chipRow(th, "projmode", kOrderedLabels, 7, &sel)) {
    s.mappingMode = kOrder[sel];
    changed = true;
  }
  ImGui::Spacing();

  bool blended = s.mappingMode == 5 || s.mappingMode == 6; // triplanar/cubic
  if (blended) {
    changed |= sliderRow(th, stripInfoIcon(T(ctx, "labels.seamBlend")).c_str(),
                         &s.mappingBlend, 0, 1, "%.2f", false, 0.01);
    changed |= sliderRow(th, stripInfoIcon(T(ctx, "labels.transitionSmoothing")).c_str(),
                         &s.seamBandWidth, 0, 1, "%.2f", false, 0.01);
  }
  if (s.mappingMode == 3) { // cylindrical
    changed |= sliderRow(th, stripInfoIcon(T(ctx, "labels.capAngle")).c_str(),
                         &s.capAngle, 1, 89, "%.0f\xc2\xb0", false, 1);
    changed |= toggleRow(th, stripInfoIcon(T(ctx, "labels.snapSeamless")).c_str(),
                         &s.snapSeamlessWrap);
    ImGui::Spacing();
    float half = (ImGui::GetContentRegionAvail().x - 8 * th.scale) * 0.5f;
    if (ghostButton(th, T(ctx, "ui.cylinderAutofit").c_str(), ImVec2(half, 28 * th.scale)) &&
        ctx.actions.cylinderAutofit)
      ctx.actions.cylinderAutofit();
    ImGui::SameLine(0, 8 * th.scale);
    if (ghostButton(th, T(ctx, "ui.cylinderReset").c_str(), ImVec2(half, 28 * th.scale)) &&
        ctx.actions.cylinderReset)
      ctx.actions.cylinderReset();
  }
  if (changed && ctx.actions.settingsChanged) ctx.actions.settingsChanged();
}

void uvSection(UiContext& ctx) {
  Theme& th = *ctx.theme;
  app::Settings& s = ctx.state->settings;
  bool changed = false;

  sectionHeader(th, T(ctx, "sections.transform").c_str());
  double prevU = s.scaleU;
  if (sliderRow(th, T(ctx, "labels.scaleU").c_str(), &s.scaleU, 0.05, 10, "%.2f", true, 0.05)) {
    if (s.lockScale) s.scaleV = s.scaleU;
    changed = true;
  }
  if (sliderRow(th, T(ctx, "labels.scaleV").c_str(), &s.scaleV, 0.05, 10, "%.2f", true, 0.05)) {
    if (s.lockScale) s.scaleU = s.scaleV;
    changed = true;
  }
  (void)prevU;
  changed |= toggleRow(th, "Lock U/V", &s.lockScale);
  changed |= sliderRow(th, T(ctx, "labels.offsetU").c_str(), &s.offsetU, -1, 1, "%.2f", false, 0.01);
  changed |= sliderRow(th, T(ctx, "labels.offsetV").c_str(), &s.offsetV, -1, 1, "%.2f", false, 0.01);
  changed |= sliderRow(th, T(ctx, "labels.rotation").c_str(), &s.rotation, 0, 360,
                       "%.0f\xc2\xb0", false, 1);
  if (changed && ctx.actions.settingsChanged) ctx.actions.settingsChanged();
}

void displacementSection(UiContext& ctx) {
  Theme& th = *ctx.theme;
  app::AppState& st = *ctx.state;
  app::Settings& s = st.settings;
  bool changed = false;

  sectionHeader(th, T(ctx, "sections.displacement").c_str(), true, true);
  changed |= sliderRow(th, T(ctx, "labels.textureHeight").c_str(), &s.amplitude, 0, 2,
                       "%.2f", false, 0.01);
  // checkAmplitudeWarning(): textureHeight > 10% of the model's smallest
  // dimension risks the bump swallowing thin features.
  const core::Bounds& b = st.meshBounds;
  const double minDim = std::min({b.size.x, b.size.y, b.size.z});
  if (s.amplitude > minDim * 0.1) {
    ImGui::PushFont(th.fonts.mono10);
    ImGui::PushStyleColor(ImGuiCol_Text, col::kAmber);
    ImGui::TextWrapped("%s", stripWarningIcon(T(ctx, "warnings.textureHeightOverlap")).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }
  changed |= toggleRow(th, T(ctx, "labels.invertDisplacement").c_str(), &s.invertDisplacement);
  changed |= toggleRow(th, stripInfoIcon(T(ctx, "labels.symmetricDisplacement")).c_str(),
                       &s.symmetricDisplacement);
  changed |= toggleRow(th, T(ctx, "ui.noDownwardZ").c_str(), &s.noDownwardZ);

  bool viewChanged = false;
  const bool prevDispPreview = st.displacementPreview3D;
  toggleRow(th, stripInfoIcon(T(ctx, "labels.displacementPreview")).c_str(),
           &st.displacementPreview3D);
  if (prevDispPreview != st.displacementPreview3D &&
      ctx.actions.toggleDisplacementPreview)
    ctx.actions.toggleDisplacementPreview(st.displacementPreview3D);
  viewChanged |= toggleRow(th, "Live preview", &st.previewEnabled);
  if (viewChanged && ctx.actions.viewChanged) ctx.actions.viewChanged();
  if (changed && ctx.actions.settingsChanged) ctx.actions.settingsChanged();
}

void maskingSection(UiContext& ctx) {
  Theme& th = *ctx.theme;
  app::AppState& st = *ctx.state;
  app::Settings& s = st.settings;
  float scale = th.scale;
  bool changed = false;

  bool maskChanged = false;

  sectionHeader(th, T(ctx, "sections.masking").c_str());
  maskChanged |= sliderRow(th, T(ctx, "labels.bottomFaces").c_str(), &s.bottomAngleLimit,
                           0, 90, "%.0f\xc2\xb0", false, 1);
  maskChanged |= sliderRow(th, T(ctx, "labels.topFaces").c_str(), &s.topAngleLimit, 0, 90,
                           "%.0f\xc2\xb0", false, 1);
  changed |= toggleRow(th, T(ctx, "ui.smoothBottom").c_str(), &s.smoothBottom);
  maskChanged |= sliderRow(th, stripInfoIcon(T(ctx, "labels.boundaryFalloff")).c_str(),
                           &s.boundaryFalloff, 0, 10, "%.1f mm", false, 0.1);
  bool prevPrecision = st.precisionMasking;
  toggleRow(th, stripInfoIcon(T(ctx, "precision.label")).c_str(), &st.precisionMasking);
  if (prevPrecision != st.precisionMasking && ctx.actions.togglePrecisionMasking)
    ctx.actions.togglePrecisionMasking(st.precisionMasking);
  if (st.precisionMasking) {
    ImGui::PushFont(th.fonts.mono10);
    ImGui::PushStyleColor(ImGuiCol_Text, col::white(0.35f));
    ImGui::TextUnformatted(st.precisionStatusText.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (st.precisionOutdated) {
      ImGui::PushStyleColor(ImGuiCol_Text, col::kAccent);
      ImGui::TextUnformatted(stripWarningIcon(T(ctx, "precision.outdated")).c_str());
      ImGui::PopStyleColor();
      ImGui::SameLine();
      if (ghostButton(th, "Refresh", ImVec2(0, 24 * scale)) &&
          ctx.actions.refreshPrecisionMesh)
        ctx.actions.refreshPrecisionMesh();
    }
  }
  changed |= maskChanged;
  if (maskChanged && ctx.actions.maskSettingsChanged)
    ctx.actions.maskSettingsChanged();

  ImGui::Spacing();
  // Heading matches the original's two conditional headings for this
  // control group — "By Surface" while excluding, "Surface Selection"
  // while the Include-Only mode is active (sections.surfaceMasking vs.
  // sections.surfaceSelection).
  const bool includeMode = st.brushMode == app::BrushMode::Include;
  ImGui::PushFont(th.fonts.sansMed12);
  ImGui::PushStyleColor(ImGuiCol_Text, col::white(0.6f));
  ImGui::TextUnformatted(
      T(ctx, includeMode ? "sections.surfaceSelection" : "sections.surfaceMasking").c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Spacing();

  bool wasPainting = st.paintingEnabled;
  toggleRow(th, "Paint exclusions", &st.paintingEnabled);
  if (st.paintingEnabled) {
    const std::string modeLabels[2] = {T(ctx, "excl.modeExclude"), T(ctx, "excl.modeIncludeOnly")};
    const char* kModes[2] = {modeLabels[0].c_str(), modeLabels[1].c_str()};
    int mode = st.brushMode == app::BrushMode::Exclude ? 0 : 1;
    if (chipRow(th, "brushmode", kModes, 2, &mode))
      st.brushMode = mode == 0 ? app::BrushMode::Exclude
                               : app::BrushMode::Include;
    ImGui::Spacing();
    static const char* kTools[3] = {"Single", "Radius", "Bucket"};
    int tool = (int)st.brushTool;
    if (chipRow(th, "brushtool", kTools, 3, &tool))
      st.brushTool = (app::BrushTool)tool;
    ImGui::Spacing();
    if (st.brushTool == app::BrushTool::Radius)
      sliderRow(th, T(ctx, "labels.size").c_str(), &st.brushRadius, 0.2, 100, "%.1f mm",
                false, 0.2);
    if (st.brushTool == app::BrushTool::Bucket)
      sliderRow(th, T(ctx, "labels.maxAngle").c_str(), &st.bucketAngle, 0, 180,
                "%.0f\xc2\xb0", false, 1);

    const char* countKey =
        st.excludedFaceCount == 1
            ? (includeMode ? "excl.faceSelected" : "excl.faceExcluded")
            : (includeMode ? "excl.facesSelected" : "excl.facesExcluded");
    const std::string count =
        T(ctx, countKey, {{"n", std::to_string(st.excludedFaceCount)}});
    ImGui::PushFont(th.fonts.mono10);
    ImGui::PushStyleColor(ImGuiCol_Text, col::white(0.35f));
    ImGui::TextUnformatted(count.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (ghostButton(th, T(ctx, "ui.clearAll").c_str(), ImVec2(0, 28 * scale)) &&
        ctx.actions.clearExclusions)
      ctx.actions.clearExclusions();
  }
  if (wasPainting != st.paintingEnabled && ctx.actions.viewChanged)
    ctx.actions.viewChanged();
  if (changed && ctx.actions.settingsChanged) ctx.actions.settingsChanged();
}

void resolutionSection(UiContext& ctx) {
  Theme& th = *ctx.theme;
  app::Settings& s = ctx.state->settings;
  float scale = th.scale;
  bool changed = false;

  sectionHeader(th, "Resolution & Quality");
  changed |= sliderRow(th, T(ctx, "labels.resolution").c_str(), &s.refineLength, 0.01, 2,
                       "%.2f mm", false, 0.01);
  changed |= sliderRowInt(th, T(ctx, "labels.outputTriangles").c_str(), &s.maxTriangles,
                          10'000, 20'000'000, "%d");
  if (ghostButton(th, T(ctx, "labels.smartRes").c_str(), ImVec2(0, 30 * scale)) &&
      ctx.actions.smartResolution)
    ctx.actions.smartResolution();
  ImGui::Spacing();

  changed |= toggleRow(th, T(ctx, "ui.enableMeshRegularization").c_str(), &s.regularizeEnabled);
  if (s.regularizeEnabled && collapsingSub(th, "Regularize advanced")) {
    ImGui::Indent(8 * scale);
    changed |= sliderRow(th, "Aspect threshold", &s.regularizeAspectThreshold,
                         1, 50, "%.1f", false, 0.1);
    changed |= sliderRow(th, "Slack", &s.regularizeSlack, 1, 10, "%.1f", false,
                         0.1);
    changed |= sliderRow(th, "Aggressive slack", &s.regularizeAggressiveSlack,
                         1, 20, "%.1f", false, 0.1);
    changed |= sliderRow(th, "Extreme aspect", &s.regularizeExtremeAspect, 1,
                         50, "%.1f", false, 0.1);
    changed |= sliderRow(th, "Normal delta", &s.regularizeNormalDeg, 0, 90,
                         "%.0f\xc2\xb0", false, 1);
    changed |= sliderRow(th, "Aggressive normal", &s.regularizeAggressiveNormalDeg,
                         0, 90, "%.0f\xc2\xb0", false, 1);
    changed |= sliderRow(th, "2nd pass length", &s.regularizeSecondPassMul, 1,
                         3, "%.2fx", false, 0.05);
    ImGui::Unindent(8 * scale);
  }
  ImGui::Spacing();
  changed |= toggleRow(th, T(ctx, "ui.harvestFlat").c_str(), &s.harvestFlatFaces);
  if (s.harvestFlatFaces)
    changed |= sliderRow(th, T(ctx, "ui.harvestTol").c_str(), &s.harvestTol, 0, 1,
                         "%.3f", false, 0.005);
  if (changed && ctx.actions.settingsChanged) ctx.actions.settingsChanged();
}

// main.js has no sidebar diagnostics entry (the popup — ui/diag_panel.cpp —
// is the whole UI); this compact status row is a deliberate native-app
// addition, so a dismissed popup can be brought back without reloading the
// mesh (main.js's only path is a page/model reload).
void diagnosticsSection(UiContext& ctx) {
  Theme& th = *ctx.theme;
  app::AppState& st = *ctx.state;
  float scale = th.scale;
  sectionHeader(th, "Mesh Diagnostics");
  if (!st.diagHasFast) {
    ImGui::PushFont(th.fonts.mono10);
    ImGui::TextColored(ImVec4(1, 1, 1, 0.45f), "Load a model to check mesh integrity.");
    ImGui::PopFont();
    return;
  }
  const bool error = st.diagOpenEdges > 0 || st.diagNonManifoldEdges > 0 ||
                     (st.diagHasAdvanced && st.diagIntersectingPairs > 0);
  const bool warn = !error && (st.diagShellCount > 1 ||
                               (st.diagHasAdvanced && st.diagOverlappingPairs > 0));
  const ImU32 dot = error ? IM_COL32(0xff, 0x5f, 0x5f, 255)
                          : warn ? col::kAmber : col::kGreen;
  const std::string summary =
      error || warn ? "Issues found" : stripCheckIcon(T(ctx, "diag.meshOk"));

  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddCircleFilled(
      ImVec2(p.x + 5 * scale, p.y + 8 * scale), 3.5f * scale, dot);
  ImGui::Dummy(ImVec2(14 * scale, 0));
  ImGui::SameLine(0, 0);
  ImGui::PushFont(th.fonts.sans12);
  ImGui::TextUnformatted(summary.c_str());
  ImGui::PopFont();

  if (st.diagDismissed && ghostButton(th, "Show details", ImVec2(0, 26 * scale)))
    st.diagDismissed = false;
}

} // namespace

void drawRightPanel(UiContext& ctx) {
  const Layout& l = ctx.layout;
  Theme& th = *ctx.theme;
  app::AppState& st = *ctx.state;
  float scale = th.scale;

  drawGlassRect(ImGui::GetBackgroundDrawList(), *ctx.glass, l.rightMin,
                l.rightMax, kPanelRounding * scale);

  float footerH = 116 * scale;
  ImGui::SetNextWindowPos(l.rightMin);
  ImGui::SetNextWindowSize(ImVec2(l.rightMax.x - l.rightMin.x,
                                  l.rightMax.y - l.rightMin.y - footerH));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(18 * scale, 18 * scale));
  ImGui::Begin("##rightpanel", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);

  displacementSection(ctx);
  divider();
  projectionSection(ctx);
  divider();
  uvSection(ctx);
  divider();
  maskingSection(ctx);
  divider();
  resolutionSection(ctx);
  divider();
  diagnosticsSection(ctx);

  ImGui::End();
  ImGui::PopStyleVar();

  // Footer: export/bake actions pinned to the rail bottom
  ImVec2 fmin(l.rightMin.x, l.rightMax.y - footerH);
  ImGui::SetNextWindowPos(fmin);
  ImGui::SetNextWindowSize(ImVec2(l.rightMax.x - fmin.x, footerH));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(16 * scale, 14 * scale));
  ImGui::Begin("##rightfooter", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollbar);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddLine(fmin, ImVec2(l.rightMax.x, fmin.y), col::white(0.07f));

  if (st.pipelineRunning) {
    ImGui::PushFont(th.fonts.mono10);
    ImGui::PushStyleColor(ImGuiCol_Text, col::white(0.55f));
    ImGui::TextUnformatted(st.pipelineStage.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + 6 * scale),
                      col::white(0.09f), 999);
    dl->AddRectFilled(pos,
                      ImVec2(pos.x + width * st.pipelineProgress,
                             pos.y + 6 * scale),
                      col::kAccent, 999);
    ImGui::Dummy(ImVec2(width, 10 * scale));
  } else {
    const bool ready = st.hasTexture();
    toggleRow(th, T(ctx, "ui.bakeMaskNewFaces").c_str(), &st.bakeKeepMask);
    ImGui::Spacing();
    float half = (ImGui::GetContentRegionAvail().x - 8 * scale) * 0.5f;
    if (ghostButton(th, T(ctx, "ui.exportStl").c_str(), ImVec2(half, 32 * scale), ready) &&
        ready && ctx.actions.exportStl)
      ctx.actions.exportStl();
    ImGui::SameLine(0, 8 * scale);
    if (ghostButton(th, T(ctx, "ui.export3mf").c_str(), ImVec2(half, 32 * scale), ready) &&
        ready && ctx.actions.export3mf)
      ctx.actions.export3mf();
    ImGui::Spacing();
    if (accentButton(th, T(ctx, "ui.bakeTextures").c_str(), ImVec2(0, 44 * scale)) &&
        ready && ctx.actions.bake)
      ctx.actions.bake();
    if (st.triLimitWarning) {
      ImGui::PushFont(th.fonts.mono10);
      ImGui::PushStyleColor(ImGuiCol_Text, col::kAmber);
      ImGui::TextWrapped("%s", stripWarningIcon(T(ctx, "warnings.safetyCapHit")).c_str());
      ImGui::PopStyleColor();
      ImGui::PopFont();
    }
    if (!st.pipelineErrorMessage.empty()) {
      ImGui::PushFont(th.fonts.mono10);
      ImGui::PushStyleColor(ImGuiCol_Text, col::kAccent);
      ImGui::TextWrapped("%s", st.pipelineErrorMessage.c_str());
      ImGui::PopStyleColor();
      ImGui::PopFont();
      if (ghostButton(th, T(ctx, "cta.storeDismiss").c_str(), ImVec2(0, 22 * scale)))
        st.pipelineErrorMessage.clear();
    }
  }
  ImGui::End();
  ImGui::PopStyleVar();
}

} // namespace ui
