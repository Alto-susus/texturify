#include "app/custom_chrome.h"

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <windowsx.h> // GET_X_LPARAM/GET_Y_LPARAM

namespace app {

namespace {

struct ChromeState {
  WNDPROC origProc = nullptr;
  double captionHeight = 58.0;
  std::vector<ChromeRect> exemptRects;
};

ChromeState* stateFor(HWND hwnd) {
  return reinterpret_cast<ChromeState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

bool pointExempt(double x, double y, const std::vector<ChromeRect>& rects) {
  for (const ChromeRect& r : rects)
    if (x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1) return true;
  return false;
}

LRESULT CALLBACK chromeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  ChromeState* st = stateFor(hwnd);
  if (!st) return DefWindowProcW(hwnd, msg, wParam, lParam);

  switch (msg) {
    case WM_NCCALCSIZE: {
      if (!wParam) return CallWindowProcW(st->origProc, hwnd, msg, wParam, lParam);
      // Deliberately do NOT call the default handler here: its calculation
      // would shrink rgrc[0] down to the standard client rect, reserving
      // space for (and thus still painting) the native caption/border.
      // Leaving rgrc[0] as the untouched proposed window rect makes the
      // ENTIRE window client area — no non-client region left to paint a
      // title bar into — while WS_CAPTION|WS_THICKFRAME stay set on the
      // window so Aero Snap/resize/move/double-click-to-maximize all keep
      // working (handled below in WM_NCHITTEST).
      if (IsZoomed(hwnd)) {
        // Maximized windows still need this inset, or the window overhangs
        // the monitor by the invisible resize-frame thickness — the classic
        // Windows borderless-window gotcha.
        auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
        const int bx = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
        const int by = GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
        p->rgrc[0].left += bx;
        p->rgrc[0].top += by;
        p->rgrc[0].right -= bx;
        p->rgrc[0].bottom -= by;
      }
      return 0;
    }
    case WM_NCHITTEST: {
      LRESULT hit = CallWindowProcW(st->origProc, hwnd, msg, wParam, lParam);
      if (hit == HTCLIENT) {
        const double x = GET_X_LPARAM(lParam);
        const double y = GET_Y_LPARAM(lParam);
        RECT wr;
        GetWindowRect(hwnd, &wr);
        const double localY = y - wr.top;
        if (localY >= 0 && localY < st->captionHeight &&
            !pointExempt(x, y, st->exemptRects))
          return HTCAPTION;
      }
      return hit;
    }
    case WM_NCDESTROY: {
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      delete st;
      break;
    }
  }
  return CallWindowProcW(st->origProc, hwnd, msg, wParam, lParam);
}

} // namespace

bool CustomChrome::install(GLFWwindow* window) {
  HWND hwnd = glfwGetWin32Window(window);
  if (!hwnd) return false;
  _hwnd = hwnd;
  auto* st = new ChromeState();
  st->origProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
      hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(chromeWndProc)));
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
  // Force Windows to re-run WM_NCCALCSIZE now that it's hooked.
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
              SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  return true;
}

void CustomChrome::updateHitTestRegion(double captionHeightPx,
                                       std::vector<ChromeRect> exemptRects) {
  if (!_hwnd) return;
  ChromeState* st = stateFor((HWND)_hwnd);
  if (!st) return;
  st->captionHeight = captionHeightPx;
  st->exemptRects = std::move(exemptRects);
}

bool CustomChrome::isMaximized() const {
  return _hwnd && IsZoomed((HWND)_hwnd);
}

void CustomChrome::minimize() {
  if (_hwnd) ShowWindow((HWND)_hwnd, SW_MINIMIZE);
}

void CustomChrome::toggleMaximize() {
  if (!_hwnd) return;
  ShowWindow((HWND)_hwnd, IsZoomed((HWND)_hwnd) ? SW_RESTORE : SW_MAXIMIZE);
}

void CustomChrome::close() {
  if (_hwnd) PostMessageW((HWND)_hwnd, WM_CLOSE, 0, 0);
}

} // namespace app

#else // !_WIN32

namespace app {
bool CustomChrome::install(GLFWwindow*) { return false; }
void CustomChrome::updateHitTestRegion(double, std::vector<ChromeRect>) {}
bool CustomChrome::isMaximized() const { return false; }
void CustomChrome::minimize() {}
void CustomChrome::toggleMaximize() {}
void CustomChrome::close() {}
} // namespace app

#endif
