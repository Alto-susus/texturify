// Liquid-glass compositor for the Spectre UI:
//  - renders the mockup's radial-gradient window background into a texture,
//  - provides an MSAA offscreen target for the 3D viewport,
//  - composes background + viewport and produces a downsampled gaussian-blurred
//    copy that panels sample for their backdrop-blur effect.
//
// All textures are GL-origin (row 0 = bottom); draw them in ImGui with
// uv0 = (u0, v1max) / uv1 = (u1, v0min) — use the uv helpers below.
#pragma once

#include <imgui.h>

#include "render/gl_util.h"

namespace ui {

class GlassCompositor {
public:
  bool init();
  void destroy();

  // Window size in framebuffer pixels; re-allocates targets when changed.
  void resize(int w, int h);

  // Bind the MSAA viewport target (viewport rect in window pixels, y-down
  // ImGui coordinates). Render the 3D scene between begin/end.
  void beginViewport(int x, int y, int w, int h);
  void endViewport(); // resolves MSAA and rebinds the default framebuffer

  // Compose background + viewport, then blur. Call once per frame after
  // endViewport() and before UI drawing.
  void composite();

  ImTextureID backgroundTex() const { return (ImTextureID)(intptr_t)_bgTex; }
  ImTextureID viewportTex() const { return (ImTextureID)(intptr_t)_vpTex; }
  ImTextureID blurTex() const { return (ImTextureID)(intptr_t)_blurTex[0]; }

  int width() const { return _w; }
  int height() const { return _h; }
  ImVec2 viewportPos() const { return ImVec2((float)_vpX, (float)_vpY); }
  ImVec2 viewportSize() const { return ImVec2((float)_vpW, (float)_vpH); }

  // UVs for drawing a full texture upright in ImGui.
  static ImVec2 uvTopLeft() { return ImVec2(0, 1); }
  static ImVec2 uvBottomRight() { return ImVec2(1, 0); }
  // UVs of a window-pixel rect within the blurred scene texture (for glass
  // panel backdrops): pass the panel's screen rect.
  ImVec2 blurUv0(const ImVec2& rectMin) const {
    return ImVec2(rectMin.x / _w, 1.0f - rectMin.y / _h);
  }
  ImVec2 blurUv1(const ImVec2& rectMax) const {
    return ImVec2(rectMax.x / _w, 1.0f - rectMax.y / _h);
  }

private:
  struct Target {
    GLuint fbo = 0, tex = 0;
    int w = 0, h = 0;
  };
  void createTarget(Target& t, int w, int h);
  void destroyTarget(Target& t);
  void drawFullscreen(render::ShaderProgram& prog);

  int _w = 0, _h = 0;
  int _vpX = 0, _vpY = 0, _vpW = 0, _vpH = 0;

  render::ShaderProgram _gradientProg, _blitProg, _blurProg;
  GLuint _emptyVao = 0;

  GLuint _bgTex = 0, _bgFbo = 0;
  // MSAA viewport target + resolve texture
  GLuint _msFbo = 0, _msColorRb = 0, _msDepthRb = 0;
  GLuint _vpFbo = 0, _vpTex = 0;
  int _vpAllocW = 0, _vpAllocH = 0;
  // Fullres composed scene + quarter-res ping-pong blur
  Target _scene;
  GLuint _blurFbo[2] = {0, 0}, _blurTex[2] = {0, 0};
  int _blurW = 0, _blurH = 0;
};

} // namespace ui
