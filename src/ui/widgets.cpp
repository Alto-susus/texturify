#include "ui/widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui {

namespace {
ImU32 lerpCol(ImU32 a, ImU32 b, float t) {
  ImVec4 va = ImGui::ColorConvertU32ToFloat4(a);
  ImVec4 vb = ImGui::ColorConvertU32ToFloat4(b);
  return ImGui::ColorConvertFloat4ToU32(
      ImVec4(va.x + (vb.x - va.x) * t, va.y + (vb.y - va.y) * t,
             va.z + (vb.z - va.z) * t, va.w + (vb.w - va.w) * t));
}

} // namespace

// Vertical gradient fill with rounded corners: solid rounded caps (top/bottom
// r px) + a smooth square multicolor gradient between them.
void roundedGradientV(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 top,
                      ImU32 bottom, float r) {
  float h = mx.y - mn.y;
  if (h <= 0 || mx.x <= mn.x) return;
  float cap = std::min(r + 1.0f, h * 0.5f);
  float t0 = cap / h, t1 = 1.0f - cap / h;
  ImU32 capTop = lerpCol(top, bottom, t0 * 0.5f);
  ImU32 capBot = lerpCol(top, bottom, 1.0f - (1.0f - t1) * 0.5f);
  dl->AddRectFilled(mn, ImVec2(mx.x, mn.y + cap), capTop, r,
                    ImDrawFlags_RoundCornersTop);
  dl->AddRectFilled(ImVec2(mn.x, mx.y - cap), mx, capBot, r,
                    ImDrawFlags_RoundCornersBottom);
  if (t1 > t0)
    dl->AddRectFilledMultiColor(ImVec2(mn.x, mn.y + cap),
                                ImVec2(mx.x, mx.y - cap), lerpCol(top, bottom, t0),
                                lerpCol(top, bottom, t0),
                                lerpCol(top, bottom, t1),
                                lerpCol(top, bottom, t1));
}

void drawGlassRect(ImDrawList* dl, const GlassCompositor& glass, ImVec2 mn,
                   ImVec2 mx, float rounding, float tintTopAlpha,
                   float tintBottomAlpha) {
  // Blurred backdrop sample
  dl->AddImageRounded(glass.blurTex(), mn, mx, glass.blurUv0(mn),
                      glass.blurUv1(mx), IM_COL32_WHITE, rounding);
  // Panel gradient tint: rgba(30,30,36,top) → rgba(16,16,20,bottom)
  roundedGradientV(dl, mn, mx, col::rgba(30, 30, 36, tintTopAlpha),
                   col::rgba(16, 16, 20, tintBottomAlpha), rounding);
  // Border + inset top highlight
  dl->AddRect(mn, mx, col::white(0.09f), rounding, 0, 1.0f);
  dl->AddLine(ImVec2(mn.x + rounding * 0.6f, mn.y + 1),
              ImVec2(mx.x - rounding * 0.6f, mn.y + 1), col::white(0.09f),
              1.0f);
}

namespace {
// Length (1-4) of the UTF-8 sequence starting at a lead byte. Continuation
// bytes (0x80-0xBF) and invalid lead bytes fall back to 1 so a malformed
// stream still advances instead of looping.
int utf8SeqLen(unsigned char c) {
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}
} // namespace

void sectionHeader(const Theme& th, const char* label, bool accent,
                   bool glowDot) {
  ImGui::PushFont(th.fonts.sansSemi10);
  // Uppercase + tracking. Only ASCII bytes are uppercased in place; a
  // multi-byte UTF-8 sequence (Cyrillic, CJK, ...) is copied through
  // unchanged — toupper() on an individual UTF-8 continuation byte corrupts
  // the sequence (each byte no longer decodes as part of its codepoint).
  char buf[96];
  int j = 0;
  for (const char* p = label; *p && j < 94;) {
    int len = utf8SeqLen((unsigned char)*p);
    if (len == 1) {
      buf[j++] = (char)toupper((unsigned char)*p);
      p++;
    } else {
      for (int k = 0; k < len && *p && j < 94; k++) buf[j++] = *p++;
    }
  }
  buf[j] = 0;
  ImU32 c = accent ? col::kAccentText : col::white(0.45f);
  ImGui::PushStyleColor(ImGuiCol_Text, c);
  // Manual letterspacing: draw per-codepoint (not per-byte, which would
  // split multi-byte UTF-8 sequences into invalid individual "characters").
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  float x = pos.x;
  float track = 1.4f * th.scale;
  ImFont* f = th.fonts.sansSemi10;
  float fontSize = ImGui::GetFontSize();
  for (int i = 0; i < j;) {
    int len = utf8SeqLen((unsigned char)buf[i]);
    if (len > j - i) len = j - i; // truncated sequence at the buffer cap
    char one[5] = {0, 0, 0, 0, 0};
    for (int k = 0; k < len; k++) one[k] = buf[i + k];
    dl->AddText(f, fontSize, ImVec2(x, pos.y), c, one);
    x += f->CalcTextSizeA(fontSize, FLT_MAX, 0, one).x + track;
    i += len;
  }
  ImGui::PopStyleColor();
  float width = ImGui::GetContentRegionAvail().x;
  if (glowDot) {
    ImVec2 dot(pos.x + width - 5, pos.y + fontSize * 0.5f);
    dl->AddCircleFilled(dot, 6.5f, col::rgba(255, 45, 85, 0.22f));
    dl->AddCircleFilled(dot, 3.5f, col::kAccent);
  }
  ImGui::Dummy(ImVec2(width, fontSize));
  ImGui::PopFont();
  ImGui::Spacing();
}

bool accentButton(const Theme& th, const char* label, ImVec2 size) {
  ImGui::PushID(label);
  ImVec2 pos = ImGui::GetCursorScreenPos();
  if (size.x <= 0) size.x = ImGui::GetContentRegionAvail().x;
  bool clicked = ImGui::InvisibleButton("##btn", size);
  bool hovered = ImGui::IsItemHovered();
  bool held = ImGui::IsItemActive();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 mx(pos.x + size.x, pos.y + size.y);
  float r = 11.0f * th.scale;
  // glow shadow
  dl->AddRectFilled(ImVec2(pos.x - 2, pos.y + 4), ImVec2(mx.x + 2, mx.y + 8),
                    col::rgba(255, 45, 80, 0.16f), r + 4);
  ImU32 hi = held ? col::kAccentLo : (hovered ? IM_COL32(0xff, 0x6e, 0x7a, 255) : col::kAccentHi);
  ImU32 lo = held ? col::kAccentDeep : col::kAccentLo;
  roundedGradientV(dl, pos, mx, hi, lo, r);
  // inset top highlight + rounded border overlay
  dl->AddRect(pos, mx, col::white(0.32f), r, 0, 1.0f);
  ImGui::PushFont(th.fonts.sansSemi13);
  ImVec2 ts = ImGui::CalcTextSize(label);
  dl->AddText(ImVec2(pos.x + (size.x - ts.x) * 0.5f, pos.y + (size.y - ts.y) * 0.5f),
              IM_COL32_WHITE, label);
  ImGui::PopFont();
  ImGui::PopID();
  return clicked;
}

bool ghostButton(const Theme& th, const char* label, ImVec2 size,
                 bool enabled) {
  ImGui::PushID(label);
  ImGui::PushFont(th.fonts.sansMed12);
  if (size.x <= 0) {
    ImVec2 ts = ImGui::CalcTextSize(label);
    size.x = ts.x + 24 * th.scale;
  }
  ImVec2 pos = ImGui::GetCursorScreenPos();
  bool clicked = ImGui::InvisibleButton("##ghost", size) && enabled;
  bool hovered = enabled && ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 mx(pos.x + size.x, pos.y + size.y);
  float r = kButtonRounding * th.scale;
  dl->AddRectFilled(pos, mx, col::white(hovered ? 0.08f : 0.04f), r);
  dl->AddRect(pos, mx, col::white(0.08f), r, 0, 1.0f);
  ImVec2 ts = ImGui::CalcTextSize(label);
  dl->AddText(ImVec2(pos.x + (size.x - ts.x) * 0.5f, pos.y + (size.y - ts.y) * 0.5f),
              col::white(enabled ? 0.82f : 0.42f), label);
  ImGui::PopFont();
  ImGui::PopID();
  return clicked;
}

namespace {
bool sliderCore(const Theme& th, const char* label, const char* valueText,
                float* t01, bool* dragging) {
  const float scale = th.scale;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float width = ImGui::GetContentRegionAvail().x;

  // Label + value line
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::PushFont(th.fonts.sansMed12);
  dl->AddText(pos, col::white(0.74f), label);
  ImGui::PopFont();
  ImGui::PushFont(th.fonts.mono11);
  ImVec2 vts = ImGui::CalcTextSize(valueText);
  dl->AddText(ImVec2(pos.x + width - vts.x, pos.y + 1), col::kAccentText,
              valueText);
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(width, 17 * scale));

  // Track + knob (hit area is taller than the 6px visual)
  ImVec2 tpos = ImGui::GetCursorScreenPos();
  float hitH = 16 * scale;
  ImGui::InvisibleButton("##track", ImVec2(width, hitH));
  bool active = ImGui::IsItemActive();
  bool changed = false;
  if (active) {
    float mx = ImGui::GetIO().MousePos.x;
    float nt = (mx - tpos.x) / std::max(width, 1.0f);
    nt = std::clamp(nt, 0.0f, 1.0f);
    if (nt != *t01) {
      *t01 = nt;
      changed = true;
    }
  }
  *dragging = active;
  float trackY = tpos.y + hitH * 0.5f;
  float h = 3 * scale; // half of 6px
  ImVec2 tmn(tpos.x, trackY - h), tmx(tpos.x + width, trackY + h);
  dl->AddRectFilled(tmn, tmx, col::white(0.09f), 999);
  float fx = tpos.x + width * *t01;
  if (*t01 > 0.001f) {
    // fill gradient #ff5a68 → #ff2d55 with glow
    dl->AddRectFilled(ImVec2(tmn.x, tmn.y - 1), ImVec2(fx, tmx.y + 1),
                      col::rgba(255, 55, 85, 0.25f), 999);
    dl->AddRectFilledMultiColor(tmn, ImVec2(fx, tmx.y), col::kAccentHi,
                                col::kAccent, col::kAccent, col::kAccentHi);
  }
  float kr = 6.5f * scale;
  dl->AddCircleFilled(ImVec2(fx, trackY), kr + 2.5f,
                      col::rgba(255, 55, 85, 0.35f));
  dl->AddCircleFilled(ImVec2(fx, trackY), kr, IM_COL32(0xff, 0xcc, 0xd4, 255));
  dl->AddCircleFilled(ImVec2(fx - kr * 0.25f, trackY - kr * 0.3f), kr * 0.45f,
                      IM_COL32_WHITE);
  dl->AddCircle(ImVec2(fx, trackY), kr, col::white(0.55f), 0, 1.0f);
  return changed;
}
} // namespace

bool sliderRow(const Theme& th, const char* label, double* v, double vmin,
               double vmax, const char* fmt, bool log, double wheelStep) {
  ImGui::PushID(label);
  char valueText[48];
  std::snprintf(valueText, sizeof(valueText), fmt, *v);

  float t;
  if (log) {
    double lmin = std::log(vmin), lmax = std::log(vmax);
    t = (float)((std::log(std::clamp(*v, vmin, vmax)) - lmin) / (lmax - lmin));
  } else {
    t = (float)((*v - vmin) / (vmax - vmin));
  }
  t = std::clamp(t, 0.0f, 1.0f);
  bool dragging = false;
  bool changed = sliderCore(th, label, valueText, &t, &dragging);
  if (changed) {
    if (log) {
      double lmin = std::log(vmin), lmax = std::log(vmax);
      *v = std::exp(lmin + (lmax - lmin) * (double)t);
    } else {
      *v = vmin + (vmax - vmin) * (double)t;
    }
  }
  // Mouse-wheel fine tuning over the whole row
  if (wheelStep > 0 && ImGui::IsItemHovered()) {
    float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0) {
      *v = std::clamp(*v + wheel * wheelStep, vmin, vmax);
      changed = true;
    }
  }
  ImGui::Spacing();
  ImGui::PopID();
  return changed;
}

bool sliderRowInt(const Theme& th, const char* label, int* v, int vmin,
                  int vmax, const char* fmt) {
  ImGui::PushID(label);
  char valueText[48];
  std::snprintf(valueText, sizeof(valueText), fmt, *v);
  float t = (float)(*v - vmin) / std::max(vmax - vmin, 1);
  t = std::clamp(t, 0.0f, 1.0f);
  bool dragging = false;
  bool changed = sliderCore(th, label, valueText, &t, &dragging);
  if (changed) *v = vmin + (int)std::lround((double)(vmax - vmin) * t);
  ImGui::Spacing();
  ImGui::PopID();
  return changed;
}

bool chipRow(const Theme& th, const char* id, const char* const* labels,
             int count, int* selected, bool small) {
  ImGui::PushID(id);
  ImGui::PushFont(th.fonts.sansSemi12);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  bool changed = false;
  float padX = (small ? 12 : 15) * th.scale;
  float padY = (small ? 6 : 7) * th.scale;
  float x = ImGui::GetCursorScreenPos().x;
  float maxX = x + ImGui::GetContentRegionAvail().x;
  for (int i = 0; i < count; i++) {
    ImGui::PushID(i);
    ImVec2 ts = ImGui::CalcTextSize(labels[i]);
    ImVec2 size(ts.x + padX * 2, ts.y + padY * 2);
    if (i > 0) {
      ImGui::SameLine(0, 6 * th.scale);
      if (ImGui::GetCursorScreenPos().x + size.x > maxX) ImGui::NewLine();
    }
    ImVec2 pos = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##chip", size) && *selected != i) {
      *selected = i;
      changed = true;
    }
    bool hovered = ImGui::IsItemHovered();
    ImVec2 mx(pos.x + size.x, pos.y + size.y);
    float r = kChipRounding * th.scale;
    if (*selected == i) {
      dl->AddRectFilled(ImVec2(pos.x, pos.y + 3), ImVec2(mx.x, mx.y + 5),
                        col::rgba(255, 45, 80, 0.18f), r + 3);
      roundedGradientV(dl, pos, mx, col::kAccentHi, col::kAccentLo, r);
      dl->AddText(ImVec2(pos.x + padX, pos.y + padY), IM_COL32_WHITE, labels[i]);
    } else {
      if (hovered) dl->AddRectFilled(pos, mx, col::white(0.05f), r);
      dl->AddText(ImVec2(pos.x + padX, pos.y + padY), col::white(0.55f),
                  labels[i]);
    }
    ImGui::PopID();
  }
  ImGui::PopFont();
  ImGui::PopID();
  return changed;
}

bool toggleRow(const Theme& th, const char* label, bool* v) {
  ImGui::PushID(label);
  float width = ImGui::GetContentRegionAvail().x;
  float swW = 34 * th.scale, swH = 19 * th.scale;
  ImVec2 pos = ImGui::GetCursorScreenPos();
  bool clicked = ImGui::InvisibleButton("##toggle", ImVec2(width, swH));
  if (clicked) *v = !*v;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImGui::PushFont(th.fonts.sansMed12);
  ImVec2 ts = ImGui::CalcTextSize(label);
  dl->AddText(ImVec2(pos.x, pos.y + (swH - ts.y) * 0.5f), col::white(0.74f),
              label);
  ImGui::PopFont();
  ImVec2 smn(pos.x + width - swW, pos.y);
  ImVec2 smx(smn.x + swW, smn.y + swH);
  float r = swH * 0.5f;
  if (*v) {
    dl->AddRectFilled(ImVec2(smn.x - 1, smn.y + 2), ImVec2(smx.x + 1, smx.y + 4),
                      col::rgba(255, 45, 80, 0.2f), r + 2);
    roundedGradientV(dl, smn, smx, col::kAccentHi, col::kAccentLo, r);
  } else {
    dl->AddRectFilled(smn, smx, col::white(0.10f), r);
    dl->AddRect(smn, smx, col::white(0.08f), r, 0, 1.0f);
  }
  float kx = *v ? smx.x - r : smn.x + r;
  dl->AddCircleFilled(ImVec2(kx, smn.y + r), r - 3, IM_COL32_WHITE);
  ImGui::Spacing();
  ImGui::PopID();
  return clicked;
}

bool textureTile(const char* id, ImTextureID tex, bool selected, float size,
                 const char* tooltip) {
  ImGui::PushID(id);
  ImVec2 pos = ImGui::GetCursorScreenPos();
  bool clicked = ImGui::InvisibleButton("##tile", ImVec2(size, size));
  bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 mx(pos.x + size, pos.y + size);
  float r = 9.0f;
  if (tex)
    dl->AddImageRounded(tex, pos, mx, ImVec2(0, 0), ImVec2(1, 1),
                        IM_COL32_WHITE, r);
  else
    dl->AddRectFilled(pos, mx, col::rgba(40, 40, 46, 1.0f), r);
  if (selected) {
    dl->AddRect(ImVec2(pos.x - 2.5f, pos.y - 2.5f),
                ImVec2(mx.x + 2.5f, mx.y + 2.5f), col::rgba(255, 45, 85, 0.18f),
                r + 2.5f, 0, 5.0f);
    dl->AddRect(pos, mx, col::kAccent, r, 0, 1.5f);
  } else {
    dl->AddRect(pos, mx, col::white(hovered ? 0.25f : 0.09f), r, 0, 1.0f);
  }
  if (hovered && tooltip && *tooltip) ImGui::SetTooltip("%s", tooltip);
  ImGui::PopID();
  return clicked;
}

bool collapsingSub(const Theme& th, const char* label, bool defaultOpen) {
  ImGuiStorage* st = ImGui::GetStateStorage();
  ImGuiID key = ImGui::GetID(label);
  bool open = st->GetBool(key, defaultOpen);
  ImVec2 pos = ImGui::GetCursorScreenPos();
  float width = ImGui::GetContentRegionAvail().x;
  float h = 22 * th.scale;
  if (ImGui::InvisibleButton(label, ImVec2(width, h))) {
    open = !open;
    st->SetBool(key, open);
  }
  bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  // caret
  float cx = pos.x + 6, cy = pos.y + h * 0.5f;
  ImU32 c = col::white(hovered ? 0.8f : 0.55f);
  if (open)
    dl->AddTriangleFilled(ImVec2(cx - 4, cy - 2), ImVec2(cx + 4, cy - 2),
                          ImVec2(cx, cy + 3.5f), c);
  else
    dl->AddTriangleFilled(ImVec2(cx - 2, cy - 4), ImVec2(cx - 2, cy + 4),
                          ImVec2(cx + 3.5f, cy), c);
  ImGui::PushFont(th.fonts.sansMed12);
  dl->AddText(ImVec2(pos.x + 16, pos.y + (h - ImGui::GetFontSize()) * 0.5f), c,
              label);
  ImGui::PopFont();
  return open;
}

void divider() {
  ImGui::Spacing();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  dl->AddLine(pos, ImVec2(pos.x + ImGui::GetContentRegionAvail().x, pos.y),
              col::white(0.07f));
  ImGui::Dummy(ImVec2(0, 4));
}

void statusPill(const Theme& th, const char* label, ImU32 dotColor) {
  ImGui::PushFont(th.fonts.mono11);
  ImVec2 ts = ImGui::CalcTextSize(label);
  float h = 30 * th.scale;
  ImVec2 size(ts.x + 12 * 2 + 14, h);
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::Dummy(size);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 mx(pos.x + size.x, pos.y + size.y);
  dl->AddRectFilled(pos, mx, col::white(0.04f), 999);
  dl->AddRect(pos, mx, col::white(0.07f), 999, 0, 1.0f);
  ImVec2 dot(pos.x + 14, pos.y + h * 0.5f);
  dl->AddCircleFilled(dot, 6, (dotColor & 0x00ffffff) | 0x33000000);
  dl->AddCircleFilled(dot, 3, dotColor);
  dl->AddText(ImVec2(pos.x + 24, pos.y + (h - ts.y) * 0.5f), col::white(0.55f),
              label);
  ImGui::PopFont();
}

} // namespace ui
