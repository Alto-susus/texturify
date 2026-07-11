#include "ui/glass.h"

#include <algorithm>
#include <cstdio>

namespace ui {

namespace {

const char* kFullscreenVS = R"GLSL(#version 330 core
out vec2 vUv;
void main() {
  // fullscreen triangle from gl_VertexID
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  vUv = p;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

// The 1a window background:
//   radial-gradient(130% 90% at 74% -12%, rgba(255,45,80,.14), transparent 52%),
//   radial-gradient(100% 100% at 2% 108%, rgba(255,70,100,.07), transparent 46%),
//   #0a0a0e
const char* kGradientFS = R"GLSL(#version 330 core
in vec2 vUv;
out vec4 oColor;

float radial(vec2 uv, vec2 center, vec2 radius, float stopPct) {
  float t = length((uv - center) / radius);
  return 1.0 - clamp(t / stopPct, 0.0, 1.0);
}

void main() {
  vec2 uv = vec2(vUv.x, 1.0 - vUv.y); // CSS y-down
  vec3 base = vec3(10.0, 10.0, 14.0) / 255.0;
  float a1 = 0.14 * radial(uv, vec2(0.74, -0.12), vec2(1.30, 0.90), 0.52);
  vec3 c = mix(base, vec3(255.0, 45.0, 80.0) / 255.0, a1);
  float a2 = 0.07 * radial(uv, vec2(0.02, 1.08), vec2(1.00, 1.00), 0.46);
  c = mix(c, vec3(255.0, 70.0, 100.0) / 255.0, a2);
  oColor = vec4(c, 1.0);
}
)GLSL";

const char* kBlitFS = R"GLSL(#version 330 core
uniform sampler2D uTex;
in vec2 vUv;
out vec4 oColor;
void main() { oColor = vec4(texture(uTex, vUv).rgb, 1.0); }
)GLSL";

// 9-tap separable gaussian; two passes at quarter resolution ≈ the mockup's
// 22–30px CSS backdrop blur.
const char* kBlurFS = R"GLSL(#version 330 core
uniform sampler2D uTex;
uniform vec2 uDir; // (1/w, 0) or (0, 1/h)
in vec2 vUv;
out vec4 oColor;
void main() {
  const float w[5] = float[](0.227027, 0.194594, 0.121621, 0.054054, 0.016216);
  vec3 c = texture(uTex, vUv).rgb * w[0];
  for (int i = 1; i < 5; i++) {
    c += texture(uTex, vUv + uDir * float(i)).rgb * w[i];
    c += texture(uTex, vUv - uDir * float(i)).rgb * w[i];
  }
  oColor = vec4(c, 1.0);
}
)GLSL";

GLuint makeColorTex(int w, int h) {
  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return tex;
}

} // namespace

bool GlassCompositor::init() {
  bool ok = true;
  ok &= _gradientProg.build(kFullscreenVS, kGradientFS, "glass-gradient");
  ok &= _blitProg.build(kFullscreenVS, kBlitFS, "glass-blit");
  ok &= _blurProg.build(kFullscreenVS, kBlurFS, "glass-blur");
  glGenVertexArrays(1, &_emptyVao);
  return ok;
}

void GlassCompositor::destroy() {
  destroyTarget(_scene);
  if (_bgFbo) glDeleteFramebuffers(1, &_bgFbo);
  if (_bgTex) glDeleteTextures(1, &_bgTex);
  if (_msFbo) glDeleteFramebuffers(1, &_msFbo);
  if (_msColorRb) glDeleteRenderbuffers(1, &_msColorRb);
  if (_msDepthRb) glDeleteRenderbuffers(1, &_msDepthRb);
  if (_vpFbo) glDeleteFramebuffers(1, &_vpFbo);
  if (_vpTex) glDeleteTextures(1, &_vpTex);
  for (int i = 0; i < 2; i++) {
    if (_blurFbo[i]) glDeleteFramebuffers(1, &_blurFbo[i]);
    if (_blurTex[i]) glDeleteTextures(1, &_blurTex[i]);
  }
  _bgFbo = _bgTex = _msFbo = _msColorRb = _msDepthRb = _vpFbo = _vpTex = 0;
  _blurFbo[0] = _blurFbo[1] = _blurTex[0] = _blurTex[1] = 0;
  if (_emptyVao) glDeleteVertexArrays(1, &_emptyVao);
  _emptyVao = 0;
  _w = _h = 0;
}

void GlassCompositor::createTarget(Target& t, int w, int h) {
  destroyTarget(t);
  t.w = w;
  t.h = h;
  t.tex = makeColorTex(w, h);
  glGenFramebuffers(1, &t.fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         t.tex, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GlassCompositor::destroyTarget(Target& t) {
  if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
  if (t.tex) glDeleteTextures(1, &t.tex);
  t = {};
}

void GlassCompositor::drawFullscreen(render::ShaderProgram& prog) {
  prog.use();
  glBindVertexArray(_emptyVao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
}

void GlassCompositor::resize(int w, int h) {
  w = std::max(w, 1);
  h = std::max(h, 1);
  if (w == _w && h == _h) return;
  _w = w;
  _h = h;

  // Background gradient (static per size)
  if (!_bgFbo) glGenFramebuffers(1, &_bgFbo);
  if (_bgTex) glDeleteTextures(1, &_bgTex);
  _bgTex = makeColorTex(w, h);
  glBindFramebuffer(GL_FRAMEBUFFER, _bgFbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         _bgTex, 0);
  glViewport(0, 0, w, h);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  drawFullscreen(_gradientProg);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Composed scene + blur chain
  createTarget(_scene, w, h);
  _blurW = std::max(w / 4, 1);
  _blurH = std::max(h / 4, 1);
  for (int i = 0; i < 2; i++) {
    if (!_blurFbo[i]) glGenFramebuffers(1, &_blurFbo[i]);
    if (_blurTex[i]) glDeleteTextures(1, &_blurTex[i]);
    _blurTex[i] = makeColorTex(_blurW, _blurH);
    glBindFramebuffer(GL_FRAMEBUFFER, _blurFbo[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           _blurTex[i], 0);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GlassCompositor::beginViewport(int x, int y, int w, int h) {
  w = std::max(w, 1);
  h = std::max(h, 1);
  _vpX = x;
  _vpY = y;
  _vpW = w;
  _vpH = h;
  if (w != _vpAllocW || h != _vpAllocH) {
    _vpAllocW = w;
    _vpAllocH = h;
    // Resolve texture
    if (!_vpFbo) glGenFramebuffers(1, &_vpFbo);
    if (_vpTex) glDeleteTextures(1, &_vpTex);
    _vpTex = makeColorTex(w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, _vpFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           _vpTex, 0);
    // 4x MSAA color+depth renderbuffers
    if (!_msFbo) glGenFramebuffers(1, &_msFbo);
    if (_msColorRb) glDeleteRenderbuffers(1, &_msColorRb);
    if (_msDepthRb) glDeleteRenderbuffers(1, &_msDepthRb);
    glGenRenderbuffers(1, &_msColorRb);
    glBindRenderbuffer(GL_RENDERBUFFER, _msColorRb);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, w, h);
    glGenRenderbuffers(1, &_msDepthRb);
    glBindRenderbuffer(GL_RENDERBUFFER, _msDepthRb);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT24,
                                     w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, _msFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, _msColorRb);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, _msDepthRb);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      std::fprintf(stderr, "glass: viewport FBO incomplete\n");
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, _msFbo);
}

void GlassCompositor::endViewport() {
  glBindFramebuffer(GL_READ_FRAMEBUFFER, _msFbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _vpFbo);
  glBlitFramebuffer(0, 0, _vpW, _vpH, 0, 0, _vpW, _vpH, GL_COLOR_BUFFER_BIT,
                    GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GlassCompositor::composite() {
  if (!_scene.fbo) return;
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);

  // scene = background gradient
  glBindFramebuffer(GL_FRAMEBUFFER, _scene.fbo);
  glViewport(0, 0, _w, _h);
  _blitProg.use();
  _blitProg.set1i("uTex", 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, _bgTex);
  drawFullscreen(_blitProg);

  // + viewport at its rect (ImGui y-down → GL y-up)
  if (_vpTex) {
    int glY = _h - (_vpY + _vpH);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, _vpFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _scene.fbo);
    glBlitFramebuffer(0, 0, _vpW, _vpH, _vpX, glY, _vpX + _vpW, glY + _vpH,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, _scene.fbo);
  }

  // downsample scene → blur[0]
  glBindFramebuffer(GL_READ_FRAMEBUFFER, _scene.fbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _blurFbo[0]);
  glBlitFramebuffer(0, 0, _w, _h, 0, 0, _blurW, _blurH, GL_COLOR_BUFFER_BIT,
                    GL_LINEAR);

  // two separable gaussian iterations: 0→1 (H), 1→0 (V), twice
  _blurProg.use();
  _blurProg.set1i("uTex", 0);
  glActiveTexture(GL_TEXTURE0);
  glViewport(0, 0, _blurW, _blurH);
  for (int iter = 0; iter < 2; iter++) {
    glBindFramebuffer(GL_FRAMEBUFFER, _blurFbo[1]);
    glBindTexture(GL_TEXTURE_2D, _blurTex[0]);
    _blurProg.set2f("uDir", 1.0f / _blurW, 0.0f);
    drawFullscreen(_blurProg);
    glBindFramebuffer(GL_FRAMEBUFFER, _blurFbo[0]);
    glBindTexture(GL_TEXTURE_2D, _blurTex[1]);
    _blurProg.set2f("uDir", 0.0f, 1.0f / _blurH);
    drawFullscreen(_blurProg);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace ui
