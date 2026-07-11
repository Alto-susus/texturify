#include "ui/theme.h"

#include <cstdio>

namespace ui {

namespace {
ImFont* addFont(ImGuiIO& io, const std::string& path, float sizePx) {
  ImFont* f = io.Fonts->AddFontFromFileTTF(path.c_str(), sizePx);
  if (!f) std::fprintf(stderr, "theme: failed to load font %s\n", path.c_str());
  return f;
}
} // namespace

bool Theme::init(const std::string& assetDir, float uiScale, const std::string& langCode) {
  scale = uiScale;
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->Clear();
  const std::string dir = assetDir + "/fonts/";
  const std::string reg = dir + "InstrumentSans-Regular.ttf";
  const std::string med = dir + "InstrumentSans-Medium.ttf";
  const std::string semi = dir + "InstrumentSans-SemiBold.ttf";
  const std::string mono = dir + "JetBrainsMono-Regular.ttf";
  const std::string notoSans = dir + "NotoSans.ttf";
  const bool isJa = langCode == "ja", isKo = langCode == "ko";
  const std::string notoCjk = isJa ? dir + "NotoSansJP.ttf" : isKo ? dir + "NotoSansKR.ttf" : "";

  // Latin Extended-A (Turkish ğşıİ, etc.) + Cyrillic (Ukrainian), merged onto
  // every Sans weight regardless of language — cheap, and covers 8 of the 10
  // shipped languages outright. CJK is far larger (thousands of glyphs), so
  // it's only baked in when actually needed.
  {
    ImFontGlyphRangesBuilder b;
    b.AddRanges(io.Fonts->GetGlyphRangesDefault());
    static constexpr ImWchar kLatinExtendedA[] = {0x0100, 0x017F, 0};
    b.AddRanges(kLatinExtendedA);
    b.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    b.BuildRanges(&mergedRanges);
  }
  cjkRanges.clear();
  if (!notoCjk.empty()) {
    ImFontGlyphRangesBuilder b;
    b.AddRanges(isJa ? io.Fonts->GetGlyphRangesJapanese() : io.Fonts->GetGlyphRangesKorean());
    b.BuildRanges(&cjkRanges);
  }

  // Primary weight + merged Noto Sans fallback (extra scripts) + merged Noto
  // Sans JP/KR (only when active) — MergeMode fonts only fill codepoints the
  // earlier fonts in the same slot didn't already provide.
  auto addSans = [&](const std::string& path, float sizePx) -> ImFont* {
    ImFont* f = addFont(io, path, sizePx);
    if (!f) return f;
    ImFontConfig mergeCfg;
    mergeCfg.MergeMode = true;
    io.Fonts->AddFontFromFileTTF(notoSans.c_str(), sizePx, &mergeCfg, mergedRanges.Data);
    if (!notoCjk.empty())
      io.Fonts->AddFontFromFileTTF(notoCjk.c_str(), sizePx, &mergeCfg, cjkRanges.Data);
    return f;
  };

  fonts.sansMed12 = addSans(med, 12.5f * scale); // default font first
  fonts.sans12 = addSans(reg, 12.5f * scale);
  fonts.sansSemi12 = addSans(semi, 12.0f * scale);
  fonts.sansSemi13 = addSans(semi, 13.0f * scale);
  fonts.sansSemi14 = addSans(semi, 14.0f * scale);
  fonts.sansSemi10 = addSans(semi, 10.5f * scale);
  fonts.mono11 = addFont(io, mono, 11.0f * scale);
  fonts.mono10 = addFont(io, mono, 10.5f * scale);
  if (!fonts.sansMed12) return false;
  io.FontDefault = fonts.sansMed12;

  // Style: mostly custom-drawn, but keep ImGui internals on-theme for
  // popups/tooltips and any stock widgets that slip through.
  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowRounding = kPanelRounding;
  s.ChildRounding = kPanelRounding;
  s.FrameRounding = kButtonRounding;
  s.PopupRounding = 12.0f;
  s.GrabRounding = 999.0f;
  s.ScrollbarRounding = 4.0f;
  s.ScrollbarSize = 8.0f;
  s.WindowBorderSize = 0.0f;
  s.FrameBorderSize = 0.0f;
  s.WindowPadding = ImVec2(16, 16);
  s.FramePadding = ImVec2(12, 8);
  s.ItemSpacing = ImVec2(8, 8);

  ImVec4* c = s.Colors;
  auto v4 = [](ImU32 u) { return ImGui::ColorConvertU32ToFloat4(u); };
  c[ImGuiCol_Text] = v4(col::kText);
  c[ImGuiCol_TextDisabled] = v4(col::white(0.35f));
  c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0); // panels draw their own glass
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = v4(col::rgba(22, 22, 28, 0.97f));
  c[ImGuiCol_Border] = v4(col::white(0.09f));
  c[ImGuiCol_FrameBg] = v4(col::rgba(0, 0, 0, 0.25f));
  c[ImGuiCol_FrameBgHovered] = v4(col::rgba(0, 0, 0, 0.35f));
  c[ImGuiCol_FrameBgActive] = v4(col::rgba(0, 0, 0, 0.4f));
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = v4(col::white(0.12f));
  c[ImGuiCol_ScrollbarGrabHovered] = v4(col::white(0.2f));
  c[ImGuiCol_ScrollbarGrabActive] = v4(col::white(0.25f));
  c[ImGuiCol_Button] = v4(col::white(0.04f));
  c[ImGuiCol_ButtonHovered] = v4(col::white(0.08f));
  c[ImGuiCol_ButtonActive] = v4(col::white(0.12f));
  c[ImGuiCol_Header] = v4(col::rgba(255, 45, 85, 0.20f));
  c[ImGuiCol_HeaderHovered] = v4(col::rgba(255, 45, 85, 0.28f));
  c[ImGuiCol_HeaderActive] = v4(col::rgba(255, 45, 85, 0.34f));
  c[ImGuiCol_SliderGrab] = v4(col::kAccentHi);
  c[ImGuiCol_SliderGrabActive] = v4(col::kAccent);
  c[ImGuiCol_CheckMark] = v4(col::kAccentHi);
  c[ImGuiCol_Separator] = v4(col::white(0.07f));
  return true;
}

} // namespace ui
