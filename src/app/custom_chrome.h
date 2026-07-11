// Custom window chrome (Windows only): suppresses the native title bar/
// border paint while keeping native move/resize/snap/maximize behavior, so
// the app's own toolbar can act as a themed replacement title bar. Uses the
// standard "extend client area over the whole window, then re-add caption/
// resize hit-testing in WM_NCHITTEST" technique — the window keeps its
// WS_CAPTION|WS_THICKFRAME styles (so Aero Snap, the resize cursors, and
// double-click-to-maximize all keep working for free), only its non-client
// PAINT area is suppressed via WM_NCCALCSIZE.
#pragma once

#include <vector>

struct GLFWwindow;

namespace app {

// Screen-space (same coordinate space as ImGui's absolute draw-list
// coordinates in single-viewport mode) rectangle, half-open [x0,x1)x[y0,y1).
struct ChromeRect {
  double x0, y0, x1, y1;
};

class CustomChrome {
public:
  // Hooks the native window behind `window`. Safe to call once, any time
  // after glfwCreateWindow. Returns false (no-op) on non-Windows platforms
  // or if the native handle couldn't be retrieved — callers should keep
  // working with the default OS title bar in that case.
  bool install(GLFWwindow* window);

  // Call once per frame after the UI has been laid out. `captionHeightPx` is
  // the app's toolbar height in window pixels (top-anchored) — the region
  // that acts as the OS-native draggable caption. `exemptRects` are that
  // toolbar's actual interactive controls (buttons, the language popup
  // anchor, ...); points inside them are left as ordinary client clicks
  // instead of being treated as a drag.
  void updateHitTestRegion(double captionHeightPx, std::vector<ChromeRect> exemptRects);

  bool installed() const { return _hwnd != nullptr; }
  bool isMaximized() const;
  void minimize();
  void toggleMaximize();
  void close();

private:
  void* _hwnd = nullptr; // HWND, kept as void* so <windows.h> stays out of this header
};

} // namespace app
