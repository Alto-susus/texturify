// Spectre-styled custom widgets, drawn with ImDrawList to match the mockup:
// glass panels, accent/ghost buttons, chip rows, slider rows with mono value
// readouts, toggles, section headers, texture tiles.
#pragma once

#include <imgui.h>

#include "ui/glass.h"
#include "ui/theme.h"

namespace ui {

// Vertical gradient fill with rounded corners (AddRectFilledMultiColor is
// square-only): rounded caps + smooth square gradient between them.
void roundedGradientV(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 top,
                      ImU32 bottom, float r);

// Draw the glass backdrop for a panel rect: blurred scene sample + the
// panel gradient tint + 1px border + inset top highlight.
void drawGlassRect(ImDrawList* dl, const GlassCompositor& glass, ImVec2 mn,
                   ImVec2 mx, float rounding, float tintTopAlpha = 0.72f,
                   float tintBottomAlpha = 0.62f);

// Section header: 10.5px semibold uppercase, letterspaced. accent → #ff8a98
// with a glowing dot on the right (like the Displacement section).
void sectionHeader(const Theme& th, const char* label, bool accent = false,
                   bool glowDot = false);

// Primary gradient button (Export / Bake). Returns clicked.
bool accentButton(const Theme& th, const char* label, ImVec2 size);

// Toolbar-style ghost button (bordered, rgba(255,255,255,.04) fill).
bool ghostButton(const Theme& th, const char* label, ImVec2 size,
                 bool enabled = true);

// Slider row: label + mono value line, then a 6px accent-fill track with a
// glowing knob. Mouse wheel over the row nudges by wheelStep (0 = disabled).
// log=true maps the knob position logarithmically (scale sliders).
bool sliderRow(const Theme& th, const char* label, double* v, double vmin,
               double vmax, const char* fmt, bool log = false,
               double wheelStep = 0.0);
bool sliderRowInt(const Theme& th, const char* label, int* v, int vmin,
                  int vmax, const char* fmt);

// Pill chip row; returns true when the selection changed.
bool chipRow(const Theme& th, const char* id, const char* const* labels,
             int count, int* selected, bool small = true);

// Label + iOS-style switch. Returns true on toggle.
bool toggleRow(const Theme& th, const char* label, bool* v);

// Square texture tile with rounded corners and the selection ring.
// Returns clicked. size = tile edge in pixels.
bool textureTile(const char* id, ImTextureID tex, bool selected, float size,
                 const char* tooltip);

// Collapsible sub-section (advanced params); persists open state via ImGui
// storage. Returns open.
bool collapsingSub(const Theme& th, const char* label, bool defaultOpen = false);

// Thin divider line (rgba(255,255,255,.07)) with vertical spacing.
void divider();

// Status pill (toolbar): dot + mono label.
void statusPill(const Theme& th, const char* label, ImU32 dotColor);

} // namespace ui
