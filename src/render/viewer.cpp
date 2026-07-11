// Port of reference/js/viewer.js. Cameras, controls, scene helpers and
// overlays replicate the three.js r170 behavior the web app relies on,
// including its physically-based light units (intensity / PI diffuse),
// ACES filmic tone mapping at exposure 1.1 and sRGB output encoding —
// applied to every material except the custom preview ShaderMaterial,
// exactly like three.js does.
#include "render/viewer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unordered_set>

#include "render/text_texture.h"

namespace render {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// sRGB hex → linear working space (three.js Color.set with color management).
double srgbToLinear(double c) {
  return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
void hexToLinear(uint32_t hex, float out[3]) {
  out[0] = (float)srgbToLinear(((hex >> 16) & 0xff) / 255.0);
  out[1] = (float)srgbToLinear(((hex >> 8) & 0xff) / 255.0);
  out[2] = (float)srgbToLinear((hex & 0xff) / 255.0);
}

const uint32_t kGizmoColors[3] = {0xff3333, 0x33dd55, 0x4488ff};
const uint32_t kGizmoHoverColors[3] = {0xff8888, 0x88ff99, 0x88bbff};

// ── GLSL ────────────────────────────────────────────────────────────────────
// three.js tonemapping_fragment + colorspace_fragment equivalents.
const char* kToneGLSL = R"GLSL(
vec3 RRTAndODTFit(vec3 v) {
  vec3 a = v * (v + 0.0245786) - 0.000090537;
  vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
  return a / b;
}
vec3 acesToneMap(vec3 color) {
  mat3 inM = mat3(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777);
  mat3 outM = mat3(
    1.60475, -0.10208, -0.00327,
    -0.53108, 1.10813, -0.07276,
    -0.07367, -0.00605, 1.07602);
  color *= 1.1 / 0.6; // toneMappingExposure 1.1, ACES pre-scale 1/0.6
  color = outM * RRTAndODTFit(inM * color);
  return clamp(color, 0.0, 1.0);
}
vec3 linearToSRGB(vec3 c) {
  vec3 lo = c * 12.92;
  vec3 hi = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
  return mix(hi, lo, vec3(lessThanEqual(c, vec3(0.0031308))));
}
)GLSL";

const char* kFlatVS = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() { gl_Position = uMVP * vec4(aPos, 1.0); }
)GLSL";

std::string flatFS() {
  return std::string("#version 330 core\n") + kToneGLSL + R"GLSL(
uniform vec3 uColor;
uniform float uOpacity;
uniform int uToneMap;
out vec4 oColor;
void main() {
  vec3 c = uColor;
  if (uToneMap == 1) c = acesToneMap(c);
  oColor = vec4(linearToSRGB(c), uOpacity);
}
)GLSL";
}

const char* kLambertVS = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uMVP;
out vec3 vNormal;
void main() {
  vNormal = aNormal; // model = identity, world-space lighting
  gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

// MeshLambertMaterial under the scene lights: ambient white 0.4,
// dir white 1.2 from (80,120,60), dir 0x8899ff 0.4 from (-60,-20,-80).
std::string lambertFS() {
  return std::string("#version 330 core\n") + kToneGLSL + R"GLSL(
uniform vec3 uColor;
uniform float uOpacity;
in vec3 vNormal;
out vec4 oColor;
void main() {
  vec3 n = normalize(vNormal);
  if (!gl_FrontFacing) n = -n;
  vec3 L1 = normalize(vec3(80.0, 120.0, 60.0));
  vec3 L2 = normalize(vec3(-60.0, -20.0, -80.0));
  vec3 irr = vec3(0.4)
           + vec3(1.2) * clamp(dot(n, L1), 0.0, 1.0)
           + vec3(0.246201, 0.318547, 1.0) * 0.4 * clamp(dot(n, L2), 0.0, 1.0);
  vec3 c = uColor * irr * 0.3183098861837907; // BRDF_Lambert
  oColor = vec4(linearToSRGB(acesToneMap(c)), uOpacity);
}
)GLSL";
}

const char* kStandardVS = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModelView;
uniform mat4 uProj;
uniform mat3 uNormalMatrix;
out vec3 vNormal;
out vec3 vViewPos;
void main() {
  vec4 mv = uModelView * vec4(aPos, 1.0);
  vViewPos = mv.xyz;
  vNormal = uNormalMatrix * aNormal;
  gl_Position = uProj * mv;
}
)GLSL";

// MeshStandardMaterial(0xaaaacc, roughness .6, metalness .1) with the same
// GGX terms three.js uses (F_Schlick exp2 approx, Smith height-correlated V).
std::string standardFS() {
  return std::string("#version 330 core\n") + kToneGLSL + R"GLSL(
uniform vec3 uLightDir1; // view space, toward light
uniform vec3 uLightDir2;
uniform int uIsOrtho;
in vec3 vNormal;
in vec3 vViewPos;
out vec4 oColor;
const float PI = 3.141592653589793;

vec3 BRDF_GGX(vec3 L, vec3 V, vec3 N, vec3 f0, float roughness) {
  float alpha = roughness * roughness;
  vec3 H = normalize(L + V);
  float dotNL = clamp(dot(N, L), 0.0, 1.0);
  float dotNV = clamp(dot(N, V), 0.0, 1.0);
  float dotNH = clamp(dot(N, H), 0.0, 1.0);
  float dotVH = clamp(dot(V, H), 0.0, 1.0);
  float fresnel = exp2((-5.55473 * dotVH - 6.98316) * dotVH);
  vec3 F = f0 * (1.0 - fresnel) + vec3(fresnel);
  float a2 = alpha * alpha;
  float gv = dotNL * sqrt(a2 + (1.0 - a2) * dotNV * dotNV);
  float gl = dotNV * sqrt(a2 + (1.0 - a2) * dotNL * dotNL);
  float Vis = 0.5 / max(gv + gl, 1e-6);
  float denom = dotNH * dotNH * (a2 - 1.0) + 1.0;
  float D = a2 / (PI * denom * denom);
  return F * (Vis * D);
}

void main() {
  vec3 base = vec3(0.401978, 0.401978, 0.603827); // 0xaaaacc linear
  float roughness = 0.6;
  float metalness = 0.1;
  vec3 diffuse = base * (1.0 - metalness);
  vec3 f0 = mix(vec3(0.04), base, metalness);

  vec3 N = normalize(vNormal);
  if (!gl_FrontFacing) N = -N;
  vec3 V = (uIsOrtho == 1) ? vec3(0.0, 0.0, 1.0) : normalize(-vViewPos);

  vec3 outgoing = vec3(0.4) * diffuse / PI; // ambient white 0.4

  vec3 lc1 = vec3(1.2);
  float dotNL1 = clamp(dot(N, uLightDir1), 0.0, 1.0);
  outgoing += dotNL1 * lc1 * diffuse / PI;
  outgoing += dotNL1 * lc1 * BRDF_GGX(uLightDir1, V, N, f0, roughness);

  vec3 lc2 = vec3(0.246201, 0.318547, 1.0) * 0.4;
  float dotNL2 = clamp(dot(N, uLightDir2), 0.0, 1.0);
  outgoing += dotNL2 * lc2 * diffuse / PI;
  outgoing += dotNL2 * lc2 * BRDF_GGX(uLightDir2, V, N, f0, roughness);

  oColor = vec4(linearToSRGB(acesToneMap(outgoing)), 1.0);
}
)GLSL";
}

// three.js LineMaterial (screen-space linewidth) — simplified to the
// non-dashed, non-worldUnits path without round end caps.
const char* kFatLineVS = R"GLSL(#version 330 core
layout(location = 0) in vec2 aCorner; // x in {-1,1}, y in {0,1}
layout(location = 1) in vec3 aStart;
layout(location = 2) in vec3 aEnd;
uniform mat4 uModelView;
uniform mat4 uProj;
uniform vec2 uResolution;
uniform float uLinewidth;

void trimSegment(in vec4 start, inout vec4 end) {
  float a = uProj[2][2];
  float b = uProj[3][2];
  float nearEstimate = -0.5 * b / a;
  float alpha = (nearEstimate - start.z) / (end.z - start.z);
  end.xyz = mix(start.xyz, end.xyz, alpha);
}

void main() {
  float aspect = uResolution.x / uResolution.y;
  vec4 start = uModelView * vec4(aStart, 1.0);
  vec4 end = uModelView * vec4(aEnd, 1.0);
  bool perspective = (uProj[2][3] == -1.0);
  if (perspective) {
    if (start.z < 0.0 && end.z >= 0.0) trimSegment(start, end);
    else if (end.z < 0.0 && start.z >= 0.0) trimSegment(end, start);
  }
  vec4 clipStart = uProj * start;
  vec4 clipEnd = uProj * end;
  vec3 ndcStart = clipStart.xyz / clipStart.w;
  vec3 ndcEnd = clipEnd.xyz / clipEnd.w;
  vec2 dir = ndcEnd.xy - ndcStart.xy;
  dir.x *= aspect;
  dir = normalize(dir);
  vec2 offset = vec2(dir.y, -dir.x);
  dir.x /= aspect;
  offset.x /= aspect;
  if (aCorner.x < 0.0) offset *= -1.0;
  offset *= uLinewidth;
  offset /= uResolution.y;
  vec4 clip = (aCorner.y < 0.5) ? clipStart : clipEnd;
  offset *= clip.w;
  clip.xy += offset;
  gl_Position = clip;
}
)GLSL";

std::string fatLineFS() {
  return std::string("#version 330 core\n") + kToneGLSL + R"GLSL(
uniform vec3 uColor;
uniform float uOpacity;
out vec4 oColor;
void main() { oColor = vec4(linearToSRGB(acesToneMap(uColor)), uOpacity); }
)GLSL";
}

const char* kSpriteVS = R"GLSL(#version 330 core
layout(location = 0) in vec2 aCorner; // ±0.5
uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uCenter;
uniform vec2 uScale;
out vec2 vUv;
void main() {
  vec4 mv = uView * vec4(uCenter, 1.0);
  mv.xy += aCorner * uScale;
  vUv = aCorner + 0.5;
  gl_Position = uProj * mv;
}
)GLSL";

// Canvas label textures have no assigned color space in the JS app, so the
// texels pass through un-decoded before tone mapping — replicated here.
std::string texFS() {
  return std::string("#version 330 core\n") + kToneGLSL + R"GLSL(
uniform sampler2D uTex;
in vec2 vUv;
out vec4 oColor;
void main() {
  vec4 t = texture(uTex, vUv);
  oColor = vec4(linearToSRGB(acesToneMap(t.rgb)), t.a);
}
)GLSL";
}

const char* kTexQuadVS = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
uniform mat4 uMVP;
out vec2 vUv;
void main() {
  vUv = aUv;
  gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

// ── Geometry generators (triangle soups; flat-colored, no normals) ─────────
void pushTri(std::vector<float>& out, const Vec3& a, const Vec3& b,
             const Vec3& c) {
  out.insert(out.end(), {(float)a.x, (float)a.y, (float)a.z,
                         (float)b.x, (float)b.y, (float)b.z,
                         (float)c.x, (float)c.y, (float)c.z});
}

// ConeGeometry(radius, height, radialSegments) with bottom cap, +Y axis,
// centered on the origin, then rotated/translated into place.
std::vector<float> coneSoup(double radius, double height, int segs,
                            const Quat& rot, const Vec3& pos) {
  std::vector<float> out;
  auto xf = [&](const Vec3& v) { return rot.rotate(v) + pos; };
  Vec3 apex = xf({0, height / 2, 0});
  Vec3 capCenter = xf({0, -height / 2, 0});
  for (int i = 0; i < segs; i++) {
    double t0 = (double)i / segs * 2 * kPi, t1 = (double)(i + 1) / segs * 2 * kPi;
    Vec3 b0 = xf({radius * std::sin(t0), -height / 2, radius * std::cos(t0)});
    Vec3 b1 = xf({radius * std::sin(t1), -height / 2, radius * std::cos(t1)});
    pushTri(out, apex, b0, b1);
    pushTri(out, capCenter, b1, b0);
  }
  return out;
}

// SphereGeometry(1, 16, 10) unit sphere.
std::vector<float> sphereSoup(int widthSegs, int heightSegs) {
  std::vector<float> out;
  auto pt = [&](int ix, int iy) -> Vec3 {
    double u = (double)ix / widthSegs, v = (double)iy / heightSegs;
    double phi = v * kPi, theta = u * 2 * kPi;
    return {-std::cos(theta) * std::sin(phi), std::cos(phi),
            std::sin(theta) * std::sin(phi)};
  };
  for (int iy = 0; iy < heightSegs; iy++)
    for (int ix = 0; ix < widthSegs; ix++) {
      Vec3 a = pt(ix, iy), b = pt(ix, iy + 1), c = pt(ix + 1, iy + 1),
           d = pt(ix + 1, iy);
      if (iy != 0) pushTri(out, a, b, d);
      if (iy != heightSegs - 1) pushTri(out, b, c, d);
    }
  return out;
}

// TorusGeometry(1, tube, radialSegs, tubularSegs) in the XY plane (z-ring),
// rotated per axis like the JS gizmo rings (x: rotY 90°, y: rotX 90°).
std::vector<float> torusSoup(double tube, int radialSegs, int tubularSegs,
                             int axis) {
  std::vector<float> out;
  auto pt = [&](int i, int j) -> Vec3 {
    double u = (double)i / tubularSegs * 2 * kPi;
    double v = (double)j / radialSegs * 2 * kPi;
    double x = (1 + tube * std::cos(v)) * std::cos(u);
    double y = (1 + tube * std::cos(v)) * std::sin(u);
    double z = tube * std::sin(v);
    if (axis == 0) return {z, y, -x};  // rotation.y = PI/2
    if (axis == 1) return {x, -z, y};  // rotation.x = PI/2
    return {x, y, z};
  };
  for (int i = 0; i < tubularSegs; i++)
    for (int j = 0; j < radialSegs; j++) {
      Vec3 a = pt(i, j), b = pt(i + 1, j), c = pt(i + 1, j + 1),
           d = pt(i, j + 1);
      pushTri(out, a, b, d);
      pushTri(out, b, c, d);
    }
  return out;
}

void uploadSoup(GlMesh& mesh, const std::vector<float>& soup) {
  mesh.upload({{0, 3}}, {soup.data()}, soup.size() / 3);
}

// Flat per-face normals (three.js computeVertexNormals on non-indexed
// geometry).
std::vector<float> flatNormals(const std::vector<float>& pos) {
  std::vector<float> n(pos.size());
  for (size_t t = 0; t + 8 < pos.size(); t += 9) {
    Vec3 a{pos[t], pos[t + 1], pos[t + 2]};
    Vec3 b{pos[t + 3], pos[t + 4], pos[t + 5]};
    Vec3 c{pos[t + 6], pos[t + 7], pos[t + 8]};
    Vec3 fn = (c - b).cross(a - b).normalized();
    for (int k = 0; k < 3; k++) {
      n[t + k * 3] = (float)fn.x;
      n[t + k * 3 + 1] = (float)fn.y;
      n[t + k * 3 + 2] = (float)fn.z;
    }
  }
  return n;
}

Mat4 rotationZ(double angle) {
  double c = std::cos(angle), s = std::sin(angle);
  return Mat4::fromRowMajor(c, -s, 0, 0, s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
}

// Edge key for WireframeGeometry-style dedup. JS hashes on the stringified
// float values, where String(-0) === "0", so normalize -0 before keying.
struct EdgeKey {
  float v[6];
  bool operator==(const EdgeKey& o) const {
    return std::memcmp(v, o.v, sizeof(v)) == 0;
  }
};
struct EdgeKeyHash {
  size_t operator()(const EdgeKey& k) const {
    uint64_t h = 1469598103934665603ull;
    const unsigned char* p = (const unsigned char*)k.v;
    for (size_t i = 0; i < sizeof(k.v); i++) h = (h ^ p[i]) * 1099511628211ull;
    return (size_t)h;
  }
};

} // namespace

// ── FatLines ────────────────────────────────────────────────────────────────

void Viewer::FatLines::build(const float* segments6f, size_t count) {
  destroy();
  segCount = count;
  if (count == 0) return;
  static const float corners[8] = {-1, 0, 1, 0, -1, 1, 1, 1};
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(1, &cornerVbo);
  glBindBuffer(GL_ARRAY_BUFFER, cornerVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glGenBuffers(1, &instVbo);
  glBindBuffer(GL_ARRAY_BUFFER, instVbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * 6 * sizeof(float)),
               segments6f, GL_STATIC_DRAW);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
  glVertexAttribDivisor(1, 1);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
  glVertexAttribDivisor(2, 1);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Viewer::FatLines::destroy() {
  if (vao) glDeleteVertexArrays(1, &vao);
  if (cornerVbo) glDeleteBuffers(1, &cornerVbo);
  if (instVbo) glDeleteBuffers(1, &instVbo);
  vao = cornerVbo = instVbo = 0;
  segCount = 0;
}

// ── Init / teardown ─────────────────────────────────────────────────────────

bool Viewer::init(const std::string& assetDir) {
  _assetDir = assetDir;
  bool ok = true;
  ok &= _flatProg.build(kFlatVS, flatFS().c_str(), "flat");
  ok &= _lambertProg.build(kLambertVS, lambertFS().c_str(), "lambert");
  ok &= _standardProg.build(kStandardVS, standardFS().c_str(), "standard");
  ok &= _fatLineProg.build(kFatLineVS, fatLineFS().c_str(), "fatline");
  ok &= _spriteProg.build(kSpriteVS, texFS().c_str(), "sprite");
  ok &= _texQuadProg.build(kTexQuadVS, texFS().c_str(), "texquad");
  ok &= _previewMat.init();
  if (!initTextRasterizer(assetDir + "/fonts/InstrumentSans-Bold.ttf"))
    std::fprintf(stderr, "viewer: label font missing (%s)\n",
                 (assetDir + "/fonts/InstrumentSans-Bold.ttf").c_str());

  buildGrid();
  uploadSoup(_pivotSphere, sphereSoup(16, 10));
  {
    static const float corners[8] = {-0.5f, -0.5f, 0.5f, -0.5f,
                                     -0.5f, 0.5f,  0.5f, 0.5f};
    _spriteQuad.upload({{0, 2}}, {corners}, 4);
    static const float quadPos[12] = {-0.5f, -0.5f, 0, 0.5f, -0.5f, 0,
                                      -0.5f, 0.5f,  0, 0.5f, 0.5f,  0};
    static const float quadUv[8] = {0, 0, 1, 0, 0, 1, 1, 1};
    _labelQuad.upload({{0, 3}, {1, 2}}, {quadPos, quadUv}, 4);
  }
  buildGizmo();
  _needsRender = true;
  return ok;
}

void Viewer::destroy() {
  _wireframe.destroy();
  _diagEdges.destroy();
  _diagFaces.clear();
  _ownedMeshes.clear();
  _axisCones.clear();
  _axisLines.clear();
  _sprites.clear();
  _flatQuads.clear();
  if (!_ownedTextures.empty()) {
    glDeleteTextures((GLsizei)_ownedTextures.size(), _ownedTextures.data());
    _ownedTextures.clear();
  }
  _previewMat.destroy();
}

// ── Scene content ───────────────────────────────────────────────────────────

void Viewer::buildGrid() {
  // GridHelper(200, 40, 0x333340, 0x2a2a34), rotated into XY for Z-up.
  const int divisions = 40;
  const double half = 100, step = 5;
  std::vector<float> center, lines;
  for (int i = 0; i <= divisions; i++) {
    double k = -half + i * step;
    auto& dst = (i == divisions / 2) ? center : lines;
    dst.insert(dst.end(), {(float)-half, (float)k, 0, (float)half, (float)k, 0,
                           (float)k, (float)-half, 0, (float)k, (float)half, 0});
  }
  _gridMajor.mesh.upload({{0, 3}}, {center.data()}, center.size() / 3);
  hexToLinear(0x333340, _gridMajor.color);
  _gridMajor.toneMapped = false; // GridHelper material sets toneMapped:false
  _gridMinor.mesh.upload({{0, 3}}, {lines.data()}, lines.size() / 3);
  hexToLinear(0x2a2a34, _gridMinor.color);
  _gridMinor.toneMapped = false;
}

void Viewer::buildAxesIndicator(double size, const Vec3& origin) {
  const double r = size;
  struct AxisDef { Vec3 dir; uint32_t hex; const char* label; };
  const AxisDef axes[3] = {{{1, 0, 0}, 0xff3333, "X"},
                           {{0, 1, 0}, 0x33dd55, "Y"},
                           {{0, 0, 1}, 0x4488ff, "Z"}};
  for (const auto& ax : axes) {
    // Shaft
    LineBatch shaft;
    Vec3 tip = origin + ax.dir * (r * 0.78);
    float seg[6] = {(float)origin.x, (float)origin.y, (float)origin.z,
                    (float)tip.x,    (float)tip.y,    (float)tip.z};
    shaft.mesh.upload({{0, 3}}, {seg}, 2);
    hexToLinear(ax.hex, shaft.color);
    shaft.opacity = 0.9f;
    _axisLines.push_back(std::move(shaft));

    // Cone arrowhead: +Y unit cone rotated onto dir
    // (Quaternion.setFromUnitVectors(+Y, dir)).
    Vec3 from{0, 1, 0};
    Vec3 cr = from.cross(ax.dir);
    Quat q = Quat{cr.x, cr.y, cr.z, 1.0 + from.dot(ax.dir)}.normalized();
    auto cone = std::make_unique<GlMesh>();
    uploadSoup(*cone, coneSoup(r * 0.07, r * 0.22, 8, q,
                               origin + ax.dir * (r * 0.89)));
    _axisCones.push_back({cone.get(), ax.hex});
    _ownedMeshes.push_back(std::move(cone));

    // Label sprite: 64x64 canvas, bold 48px
    TextBitmap bmp = rasterizeLabel(ax.label, 64, 64, 48, ax.hex);
    GLuint tex = createTextureRGBA(bmp.rgba.data(), bmp.width, bmp.height,
                                   false, true, true, true);
    _ownedTextures.push_back(tex);
    Sprite s;
    s.tex = tex;
    s.pos = origin + ax.dir * (r * 1.18);
    s.sx = r * 0.32;
    s.sy = r * 0.32;
    _sprites.push_back(s);
  }
}

void Viewer::buildDimensions(const Vec3& boxMin, const Vec3& boxMax,
                             double groundZ, double scale) {
  const double pad = scale * 0.18, tick = scale * 0.08;
  const double lblW = scale * 0.50, lblH = scale * 0.12, zOff = 0.02;
  char text[64];

  auto addBatch = [&](std::vector<float>& segs, uint32_t hex) {
    LineBatch b;
    b.mesh.upload({{0, 3}}, {segs.data()}, segs.size() / 3);
    hexToLinear(hex, b.color);
    b.opacity = 0.75f;
    _axisLines.push_back(std::move(b));
  };
  auto addLabel = [&](const char* txt, uint32_t hex, const Mat4& model) {
    TextBitmap bmp = rasterizeLabel(txt, 256, 64, 36, hex);
    GLuint tex = createTextureRGBA(bmp.rgba.data(), bmp.width, bmp.height,
                                   false, true, true, true);
    _ownedTextures.push_back(tex);
    _flatQuads.push_back({tex, model});
  };
  auto seg = [](std::vector<float>& dst, const Vec3& a, const Vec3& b) {
    dst.insert(dst.end(), {(float)a.x, (float)a.y, (float)a.z,
                           (float)b.x, (float)b.y, (float)b.z});
  };

  // X dimension along the front edge
  {
    const uint32_t hex = 0xff3333;
    double y = boxMin.y - pad;
    std::vector<float> segs;
    seg(segs, {boxMin.x, y, groundZ}, {boxMax.x, y, groundZ});
    seg(segs, {boxMin.x, y - tick * 0.5, groundZ}, {boxMin.x, y + tick * 0.5, groundZ});
    seg(segs, {boxMax.x, y - tick * 0.5, groundZ}, {boxMax.x, y + tick * 0.5, groundZ});
    addBatch(segs, hex);
    std::snprintf(text, sizeof(text), "X: %.2f", boxMax.x - boxMin.x);
    Mat4 model = makeTranslation({(boxMin.x + boxMax.x) / 2, y - lblH * 0.7,
                                  groundZ + zOff})
                     .multiplied(Mat4::makeScale(lblW, lblH, 1));
    addLabel(text, hex, model);
  }

  // Y dimension along the right edge
  {
    const uint32_t hex = 0x33dd55;
    double x = boxMax.x + pad;
    std::vector<float> segs;
    seg(segs, {x, boxMin.y, groundZ}, {x, boxMax.y, groundZ});
    seg(segs, {x - tick * 0.5, boxMin.y, groundZ}, {x + tick * 0.5, boxMin.y, groundZ});
    seg(segs, {x - tick * 0.5, boxMax.y, groundZ}, {x + tick * 0.5, boxMax.y, groundZ});
    addBatch(segs, hex);
    std::snprintf(text, sizeof(text), "Y: %.2f", boxMax.y - boxMin.y);
    Mat4 model = makeTranslation({x + lblH * 0.7, (boxMin.y + boxMax.y) / 2,
                                  groundZ + zOff})
                     .multiplied(rotationZ(kPi / 2))
                     .multiplied(Mat4::makeScale(lblW, lblH, 1));
    addLabel(text, hex, model);
  }
}

namespace {
void uploadMeshBuffers(GlMesh& mesh, const core::Geometry& geo,
                       const PreviewAttributes& attrs,
                       std::vector<float>& positionsOut) {
  positionsOut = geo.positions;
  size_t verts = geo.positions.size() / 3;
  std::vector<float> computedNormals;
  const float* normals = geo.normals.size() == geo.positions.size()
                             ? geo.normals.data()
                             : nullptr;
  if (!normals) {
    computedNormals = flatNormals(geo.positions);
    normals = computedNormals.data();
  }
  std::vector<VertexAttrib> attribs = {{ATTR_POSITION, 3}, {ATTR_NORMAL, 3}};
  std::vector<const float*> buffers = {geo.positions.data(), normals};
  auto addOpt = [&](const std::vector<float>* v, GLuint loc, GLint comps) {
    if (v && v->size() == verts * (size_t)comps) {
      attribs.push_back({loc, comps});
      buffers.push_back(v->data());
    }
  };
  addOpt(attrs.smoothNormal, ATTR_SMOOTH_NORMAL, 3);
  addOpt(attrs.faceNormal, ATTR_FACE_NORMAL, 3);
  addOpt(attrs.faceMask, ATTR_FACE_MASK, 1);
  addOpt(attrs.boundaryFalloff, ATTR_BOUNDARY_FALLOFF, 1);
  addOpt(attrs.boundaryMaskType, ATTR_BOUNDARY_MASK_TYPE, 1);
  mesh.upload(attribs, buffers, verts);
}
} // namespace

void Viewer::loadGeometry(const core::Geometry& geo,
                          const PreviewAttributes& attrs) {
  uploadMeshBuffers(_mesh, geo, attrs, _meshPositions);
  _meshVerts = _meshPositions.size() / 3;

  // Bounding box + sphere (three.js computeBoundingSphere: center = box
  // center, radius = max vertex distance).
  Vec3 mn{1e300, 1e300, 1e300}, mx{-1e300, -1e300, -1e300};
  for (size_t i = 0; i + 2 < _meshPositions.size(); i += 3) {
    mn.x = std::min(mn.x, (double)_meshPositions[i]);
    mn.y = std::min(mn.y, (double)_meshPositions[i + 1]);
    mn.z = std::min(mn.z, (double)_meshPositions[i + 2]);
    mx.x = std::max(mx.x, (double)_meshPositions[i]);
    mx.y = std::max(mx.y, (double)_meshPositions[i + 1]);
    mx.z = std::max(mx.z, (double)_meshPositions[i + 2]);
  }
  if (_meshVerts == 0) mn = mx = Vec3{0, 0, 0};
  Vec3 center = (mn + mx) * 0.5;
  double radiusSq = 0;
  for (size_t i = 0; i + 2 < _meshPositions.size(); i += 3) {
    Vec3 p{_meshPositions[i], _meshPositions[i + 1], _meshPositions[i + 2]};
    radiusSq = std::max(radiusSq, (p - center).lengthSq());
  }
  double radius = std::sqrt(radiusSq);

  double groundZ = mn.z - 0.01;
  _gridZ = groundZ;
  fitCamera(center, radius);

  // Rebuild axes indicator + dimension annotations
  _axisLines.clear();
  _axisCones.clear();
  _ownedMeshes.clear();
  _sprites.clear();
  _flatQuads.clear();
  if (!_ownedTextures.empty()) {
    glDeleteTextures((GLsizei)_ownedTextures.size(), _ownedTextures.data());
    _ownedTextures.clear();
  }
  double axisSize = radius * 0.30;
  double axisPad = axisSize * 1.8;
  buildAxesIndicator(axisSize, {mn.x - axisPad, mn.y - axisPad, groundZ});
  buildDimensions(mn, mx, groundZ, radius);

  _wireframe.destroy();
  if (_wireVisible) buildWireframe();
  _needsRender = true;
}

void Viewer::setMeshGeometry(const core::Geometry& geo,
                             const PreviewAttributes& attrs) {
  uploadMeshBuffers(_mesh, geo, attrs, _meshPositions);
  _meshVerts = _meshPositions.size() / 3;
  _wireframe.destroy();
  if (_wireVisible) buildWireframe();
  _needsRender = true;
}

void Viewer::setWireframe(bool enabled) {
  _wireVisible = enabled;
  if (enabled && _wireframe.segCount == 0 && hasMesh()) buildWireframe();
  _needsRender = true;
}

void Viewer::buildWireframe() {
  // WireframeGeometry: every triangle edge, deduped by endpoint positions.
  std::unordered_set<EdgeKey, EdgeKeyHash> seen;
  std::vector<float> segs;
  auto norm0 = [](float v) { return v == 0.0f ? 0.0f : v; };
  const auto& p = _meshPositions;
  for (size_t t = 0; t + 8 < p.size(); t += 9) {
    for (int j = 0; j < 3; j++) {
      size_t i1 = t + (size_t)j * 3, i2 = t + ((size_t)(j + 1) % 3) * 3;
      float a[3] = {norm0(p[i1]), norm0(p[i1 + 1]), norm0(p[i1 + 2])};
      float b[3] = {norm0(p[i2]), norm0(p[i2 + 1]), norm0(p[i2 + 2])};
      // canonical endpoint order so (a,b) and (b,a) collide
      bool swap = std::memcmp(a, b, sizeof(a)) > 0;
      EdgeKey key;
      std::memcpy(key.v, swap ? b : a, 12);
      std::memcpy(key.v + 3, swap ? a : b, 12);
      if (!seen.insert(key).second) continue;
      segs.insert(segs.end(), {p[i1], p[i1 + 1], p[i1 + 2],
                               p[i2], p[i2 + 1], p[i2 + 2]});
    }
  }
  _wireframe.build(segs.data(), segs.size() / 6);
}

void Viewer::setExclusionOverlay(const core::Geometry* overlayGeo,
                                 uint32_t color, double opacity) {
  if (!overlayGeo || overlayGeo->positions.empty()) {
    _exclusionMesh.destroy();
    _needsRender = true;
    return;
  }
  const float* normals = nullptr;
  std::vector<float> computed;
  if (overlayGeo->normals.size() == overlayGeo->positions.size()) {
    normals = overlayGeo->normals.data();
  } else {
    computed = flatNormals(overlayGeo->positions);
    normals = computed.data();
  }
  _exclusionMesh.upload({{0, 3}, {1, 3}},
                        {overlayGeo->positions.data(), normals},
                        overlayGeo->positions.size() / 3);
  hexToLinear(color, _exclColor);
  _exclOpacity = (float)opacity;
  _needsRender = true;
}

void Viewer::setHoverPreview(const core::Geometry* overlayGeo,
                             uint32_t color) {
  if (!overlayGeo || overlayGeo->positions.empty()) {
    _hoverMesh.destroy();
    _needsRender = true;
    return;
  }
  _hoverMesh.upload({{0, 3}}, {overlayGeo->positions.data()},
                    overlayGeo->positions.size() / 3);
  hexToLinear(color, _hoverColor);
  _needsRender = true;
}

void Viewer::setDiagEdges(const std::vector<float>& segmentPositions,
                          uint32_t color) {
  _diagEdges.destroy();
  if (!segmentPositions.empty()) {
    _diagEdges.build(segmentPositions.data(), segmentPositions.size() / 6);
    hexToLinear(color, _diagEdgeColor);
  }
  _needsRender = true;
}

void Viewer::addDiagFaces(const core::Geometry& overlayGeo, uint32_t color,
                          double opacity, bool xray) {
  if (overlayGeo.positions.empty()) return;
  auto df = std::make_unique<DiagFaces>();
  df->mesh.upload({{0, 3}}, {overlayGeo.positions.data()},
                  overlayGeo.positions.size() / 3);
  hexToLinear(color, df->color);
  df->opacity = (float)opacity;
  df->xray = xray;
  _diagFaces.push_back(std::move(df));
  _needsRender = true;
}

void Viewer::clearDiagOverlays() {
  _diagEdges.destroy();
  _diagFaces.clear();
  _needsRender = true;
}

void Viewer::setShiftLine(bool visible, const Vec3& a, const Vec3& b) {
  _shiftLineVisible = visible;
  if (visible) {
    const float seg[6] = {(float)a.x, (float)a.y, (float)a.z,
                          (float)b.x, (float)b.y, (float)b.z};
    _shiftLine.mesh.upload({{0, 3}}, {seg}, 2);
    hexToLinear(0x00ffaa, _shiftLine.color);
    _shiftLine.opacity = 1.0f;
  } else {
    _shiftLine.mesh.destroy();
  }
  _needsRender = true;
}

void Viewer::setSceneBackground(uint32_t hexColor) {
  _bg[0] = ((hexColor >> 16) & 0xff) / 255.0f;
  _bg[1] = ((hexColor >> 8) & 0xff) / 255.0f;
  _bg[2] = (hexColor & 0xff) / 255.0f;
  _needsRender = true;
}

// ── Rotation gizmo ──────────────────────────────────────────────────────────

void Viewer::buildGizmo() {
  for (int axis = 0; axis < 3; axis++) {
    uploadSoup(_gizmoRing[axis], torusSoup(0.02, 12, 64, axis));
    _gizmoHitTris[axis] = torusSoup(0.08, 8, 64, axis);
  }
}

void Viewer::updateGizmoScale(bool lock) {
  if (!hasMesh()) return;
  Vec3 mn{1e300, 1e300, 1e300}, mx{-1e300, -1e300, -1e300};
  for (size_t i = 0; i + 2 < _meshPositions.size(); i += 3) {
    mn.x = std::min(mn.x, (double)_meshPositions[i]);
    mn.y = std::min(mn.y, (double)_meshPositions[i + 1]);
    mn.z = std::min(mn.z, (double)_meshPositions[i + 2]);
    mx.x = std::max(mx.x, (double)_meshPositions[i]);
    mx.y = std::max(mx.y, (double)_meshPositions[i + 1]);
    mx.z = std::max(mx.z, (double)_meshPositions[i + 2]);
  }
  Vec3 center = (mn + mx) * 0.5;
  if (lock || !_gizmoScaleLocked) {
    double radiusSq = 0;
    for (size_t i = 0; i + 2 < _meshPositions.size(); i += 3) {
      Vec3 p{_meshPositions[i], _meshPositions[i + 1], _meshPositions[i + 2]};
      radiusSq = std::max(radiusSq, (p - center).lengthSq());
    }
    _gizmoScale = std::sqrt(radiusSq) * 0.65;
    _gizmoScaleLocked = true;
  }
  _gizmoCenter = center;
}

void Viewer::setRotationGizmo(bool visible,
                              std::function<void(int, double)> onRotate) {
  _gizmoVisible = visible;
  _gizmoCallback = std::move(onRotate);
  if (visible) {
    _gizmoScaleLocked = false; // measure fresh
    updateGizmoScale(true);
  } else {
    _gizmoDragAxis = -1;
    _gizmoScaleLocked = false;
  }
  _needsRender = true;
}

void Viewer::updateRotationGizmo() {
  if (_gizmoVisible) {
    updateGizmoScale(false);
    _needsRender = true;
  }
}

int Viewer::pickGizmoRing(double ndcX, double ndcY) const {
  if (!_gizmoVisible || _gizmoScale <= 0) return -1;
  Ray ray = screenRayNdcInternal(ndcX, ndcY);
  // Transform into gizmo-local unit space (uniform scale + translation).
  Ray local;
  local.origin = (ray.origin - _gizmoCenter) * (1.0 / _gizmoScale);
  local.dir = ray.dir;
  double bestT = 1e300;
  int bestAxis = -1;
  for (int axis = 0; axis < 3; axis++) {
    const auto& tris = _gizmoHitTris[axis];
    for (size_t t = 0; t + 8 < tris.size(); t += 9) {
      Vec3 a{tris[t], tris[t + 1], tris[t + 2]};
      Vec3 b{tris[t + 3], tris[t + 4], tris[t + 5]};
      Vec3 c{tris[t + 6], tris[t + 7], tris[t + 8]};
      double tt = local.intersectTriangle(a, b, c);
      if (tt >= 0 && tt < bestT) {
        bestT = tt;
        bestAxis = axis;
      }
    }
  }
  return bestAxis;
}

bool Viewer::gizmoPlaneAngle(double ndcX, double ndcY, int axis,
                             double& out) const {
  Ray ray = screenRayNdcInternal(ndcX, ndcY);
  Vec3 normal{axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0,
              axis == 2 ? 1.0 : 0.0};
  Vec3 pt;
  if (!ray.intersectPlane(normal, _gizmoCenter, pt)) return false;
  Vec3 local = pt - _gizmoCenter;
  if (axis == 0) out = std::atan2(local.z, local.y);
  else if (axis == 1) out = std::atan2(local.x, local.z);
  else out = std::atan2(local.y, local.x);
  return true;
}

// ── Cameras ─────────────────────────────────────────────────────────────────

Mat4 Viewer::viewMatrix() const {
  return inverted(makeTranslation(_camPos).multiplied(_camQuat.toMat4()));
}

Mat4 Viewer::projectionMatrix() const {
  double aspect = (double)_width / (double)std::max(_height, 1);
  if (_isPerspective)
    return perspectiveMatrix(_perspFov, aspect, _perspNear, _perspFar);
  double dx = _orthoHalfH * aspect / _orthoZoom;
  double dy = _orthoHalfH / _orthoZoom;
  return orthoMatrix(-dx, dx, dy, -dy, _orthoNear, _orthoFar);
}

Vec3 Viewer::unproject(double ndcX, double ndcY, double ndcZ) const {
  Mat4 world = makeTranslation(_camPos).multiplied(_camQuat.toMat4());
  return world.apply(inverted(projectionMatrix()).apply({ndcX, ndcY, ndcZ}));
}

void Viewer::lookAt(const Vec3& target) {
  _camQuat = Quat::fromMat4(lookAtRotation(_camPos, target, _camUp));
}

void Viewer::fitCamera(const Vec3& center, double radius) {
  if (radius <= 0) radius = 1;
  double halfH = radius * 1.4;
  _orthoHalfH = halfH;
  _orthoZoom = 1;
  _orthoNear = -radius * 200;
  _orthoFar = radius * 200;
  _perspNear = radius * 0.01;
  _perspFar = radius * 400;
  _target = center;
  Vec3 dir = Vec3{0.6, -1.2, 0.8}.normalized();
  if (_isPerspective) {
    double dist = halfH / std::tan(degToRad(_perspFov / 2));
    _camPos = center + dir * dist;
  } else {
    _camPos = center + dir * (halfH * 4);
  }
  _camUp = {0, 0, 1};
  lookAt(center);
}

void Viewer::setProjection(bool perspective) {
  if (perspective == _isPerspective) return;
  _isPerspective = perspective;
  if (perspective) {
    // Derive a distance from the ortho frustum so the view size matches.
    double halfH = _orthoHalfH / _orthoZoom;
    double dist = halfH / std::tan(degToRad(_perspFov / 2));
    Vec3 dir = (_camPos - _target).normalized();
    _camPos = _target + dir * dist;
  } else {
    _orthoZoom = 1;
  }
  _needsRender = true;
}

Ray Viewer::screenRayNdcInternal(double ndcX, double ndcY) const {
  Ray r;
  if (_isPerspective) {
    r.origin = _camPos;
    r.dir = (unproject(ndcX, ndcY, 0.5) - _camPos).normalized();
  } else {
    r.origin = unproject(ndcX, ndcY, -1);
    r.dir = (unproject(ndcX, ndcY, 1) - r.origin).normalized();
  }
  return r;
}

Ray Viewer::screenRay(double x, double y) const {
  double ndcX = (x / std::max(_width, 1)) * 2 - 1;
  double ndcY = -(y / std::max(_height, 1)) * 2 + 1;
  return screenRayNdcInternal(ndcX, ndcY);
}

RaycastHit Viewer::raycast(double x, double y) const {
  RaycastHit hit;
  if (!hasMesh()) return hit;
  Ray ray = screenRay(x, y);
  double bestT = 1e300;
  const auto& p = _meshPositions;
  for (size_t t = 0; t + 8 < p.size(); t += 9) {
    Vec3 a{p[t], p[t + 1], p[t + 2]};
    Vec3 b{p[t + 3], p[t + 4], p[t + 5]};
    Vec3 c{p[t + 6], p[t + 7], p[t + 8]};
    double tt = ray.intersectTriangle(a, b, c);
    if (tt >= 0 && tt < bestT) {
      bestT = tt;
      hit.hit = true;
      hit.faceIndex = (int32_t)(t / 9);
    }
  }
  if (hit.hit) {
    hit.distance = bestT;
    hit.point = ray.origin + ray.dir * bestT;
  }
  return hit;
}

RaycastHit Viewer::raycastFront(double x, double y) const {
  RaycastHit best;
  if (!hasMesh()) return best;
  Ray ray = screenRay(x, y);
  // Collect all hits sorted by distance (three.js intersectObject), then
  // return the first whose geometric normal opposes the ray (getFrontFaceHit).
  struct Hit {
    double t;
    int32_t face;
    bool front;
  };
  std::vector<Hit> hits;
  const auto& p = _meshPositions;
  for (size_t t = 0; t + 8 < p.size(); t += 9) {
    Vec3 a{p[t], p[t + 1], p[t + 2]};
    Vec3 b{p[t + 3], p[t + 4], p[t + 5]};
    Vec3 c{p[t + 6], p[t + 7], p[t + 8]};
    double tt = ray.intersectTriangle(a, b, c);
    if (tt >= 0) {
      // hit.face.normal = winding normal (identity transform → world space)
      Vec3 n = (b - a).cross(c - a);
      double len = n.length();
      if (len > 0) n = n * (1.0 / len);
      hits.push_back({tt, (int32_t)(t / 9), n.dot(ray.dir) < 0});
    }
  }
  if (hits.empty()) return best;
  std::stable_sort(hits.begin(), hits.end(),
                   [](const Hit& a, const Hit& b) { return a.t < b.t; });
  const Hit* chosen = &hits[0]; // JS fallback: hits[0]
  for (const Hit& h : hits)
    if (h.front) { chosen = &h; break; }
  best.hit = true;
  best.faceIndex = chosen->face;
  best.distance = chosen->t;
  best.point = ray.origin + ray.dir * chosen->t;
  return best;
}

void Viewer::worldToScreen(const Vec3& p, double& xOut, double& yOut) const {
  Vec3 ndc = projectionMatrix().multiplied(viewMatrix()).apply(p);
  xOut = (ndc.x * 0.5 + 0.5) * _width;
  yOut = (1 - (ndc.y * 0.5 + 0.5)) * _height;
}

// ── Input ───────────────────────────────────────────────────────────────────

bool Viewer::onPointerDown(double x, double y, int button) {
  _lastX = x;
  _lastY = y;
  double ndcX = (x / std::max(_width, 1)) * 2 - 1;
  double ndcY = -(y / std::max(_height, 1)) * 2 + 1;

  if (button == 0 && _gizmoVisible) {
    int axis = pickGizmoRing(ndcX, ndcY);
    if (axis >= 0) {
      _gizmoDragAxis = axis;
      double a;
      _gizmoLastAngle = gizmoPlaneAngle(ndcX, ndcY, axis, a) ? a : kNaN;
      _needsRender = true;
      return true;
    }
  }

  if (button == 0 && _controlsEnabled) {
    // Raycast-based orbit pivot (falls back to last pivot, then target).
    RaycastHit hit = raycast(x, y);
    if (hit.hit) {
      _orbitPivot = hit.point;
      _lastPivot = hit.point;
      _hasLastPivot = true;
    } else if (_hasLastPivot) {
      _orbitPivot = _lastPivot;
    } else {
      _orbitPivot = _target;
    }
    _orbiting = true;
    _pivotVisible = true;
    _pivotPos = _orbitPivot;
    _pivotScale =
        _isPerspective
            ? (_orbitPivot - _camPos).length() *
                  std::tan(degToRad(_perspFov / 2)) * 0.015
            : (_orthoHalfH / _orthoZoom) * 0.015;
    _needsRender = true;
    return true;
  }

  if (button == 2 && _controlsEnabled) { // right drag = pan
    _panning = true;
    return true;
  }
  return false;
}

void Viewer::onPointerMove(double x, double y) {
  double dx = x - _lastX, dy = y - _lastY;
  double ndcX = (x / std::max(_width, 1)) * 2 - 1;
  double ndcY = -(y / std::max(_height, 1)) * 2 + 1;

  if (_gizmoDragAxis >= 0) {
    _lastX = x;
    _lastY = y;
    double angle;
    if (gizmoPlaneAngle(ndcX, ndcY, _gizmoDragAxis, angle) &&
        !std::isnan(_gizmoLastAngle)) {
      double delta = angle - _gizmoLastAngle;
      if (delta > kPi) delta -= 2 * kPi;
      if (delta < -kPi) delta += 2 * kPi;
      double degrees = radToDeg(delta);
      if (std::abs(degrees) > 0.01 && _gizmoCallback)
        _gizmoCallback(_gizmoDragAxis, degrees);
      _gizmoLastAngle = angle;
    } else if (!std::isnan(_gizmoLastAngle)) {
      // keep last angle
    } else {
      double a;
      if (gizmoPlaneAngle(ndcX, ndcY, _gizmoDragAxis, a)) _gizmoLastAngle = a;
    }
    return;
  }

  // Hover highlight when the gizmo is visible
  if (_gizmoVisible) {
    int axis = pickGizmoRing(ndcX, ndcY);
    if (axis != _gizmoHoverAxis) {
      _gizmoHoverAxis = axis;
      _needsRender = true;
    }
  }

  if (_orbiting && _controlsEnabled) {
    _lastX = x;
    _lastY = y;
    if (dx == 0 && dy == 0) return;
    const double rotSpeed = 0.005;
    Vec3 camRight = _camQuat.rotate({1, 0, 0});
    Quat q = Quat::fromAxisAngle({0, 0, 1}, -dx * rotSpeed)
                 .premultiplied(Quat::fromAxisAngle(camRight, -dy * rotSpeed));
    _camPos = _orbitPivot + q.rotate(_camPos - _orbitPivot);
    _target = _orbitPivot + q.rotate(_target - _orbitPivot);
    _camQuat = _camQuat.premultiplied(q).normalized();
    _needsRender = true;
    return;
  }

  if (_panning && _controlsEnabled) {
    _lastX = x;
    _lastY = y;
    if (dx == 0 && dy == 0) return;
    double panX, panY;
    if (_isPerspective) {
      // OrbitControls.pan: half-height at target distance drives the scale
      double targetDist =
          (_camPos - _target).length() * std::tan(degToRad(_perspFov / 2));
      panX = 2 * dx * targetDist / std::max(_height, 1);
      panY = 2 * dy * targetDist / std::max(_height, 1);
    } else {
      double aspect = (double)_width / (double)std::max(_height, 1);
      panX = dx * (2 * _orthoHalfH * aspect / _orthoZoom) / std::max(_width, 1);
      panY = dy * (2 * _orthoHalfH / _orthoZoom) / std::max(_height, 1);
    }
    Vec3 right = _camQuat.rotate({1, 0, 0});
    Vec3 up = _camQuat.rotate({0, 1, 0}); // screenSpacePanning
    Vec3 offset = right * (-panX) + up * panY;
    _camPos += offset;
    _target += offset;
    _needsRender = true;
    return;
  }

  _lastX = x;
  _lastY = y;
}

void Viewer::onPointerUp(int) {
  if (_gizmoDragAxis >= 0) {
    _gizmoDragAxis = -1;
    _needsRender = true;
  }
  if (_orbiting) {
    _orbiting = false;
    _pivotVisible = false;
    // Re-level like the JS pointerup: reset up, lookAt target
    _camUp = {0, 0, 1};
    lookAt(_target);
    _needsRender = true;
  }
  _panning = false;
}

void Viewer::onScroll(double x, double y, double deltaY) {
  double ndcX = (x / std::max(_width, 1)) * 2 - 1;
  double ndcY = -(y / std::max(_height, 1)) * 2 + 1;
  if (_isPerspective) {
    double factor = deltaY > 0 ? 0.9 : 1.1;
    Vec3 dir = (unproject(ndcX, ndcY, 0.5) - _camPos).normalized();
    double dist = (_camPos - _target).length();
    double dolly = dist * (1 - 1 / factor);
    _camPos += dir * dolly;
    _target += dir * dolly;
  } else {
    Vec3 before = unproject(ndcX, ndcY, 0);
    double factor = deltaY > 0 ? 1 / 1.1 : 1.1;
    _orthoZoom = std::max(0.05, std::min(200.0, _orthoZoom * factor));
    Vec3 after = unproject(ndcX, ndcY, 0);
    Vec3 delta = before - after;
    _camPos += delta;
    _target += delta;
  }
  _needsRender = true;
}

// ── Drawing ─────────────────────────────────────────────────────────────────

void Viewer::resize(int widthPx, int heightPx) {
  _width = std::max(widthPx, 1);
  _height = std::max(heightPx, 1);
  _needsRender = true;
}

void Viewer::drawMesh(const Mat4& view, const Mat4& proj) {
  if (_mesh.empty()) return;
  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
  if (_usePreview) {
    _previewMat.bind(_previewParams, _displacementTex, view, proj);
  } else {
    _standardProg.use();
    float m16[16];
    mat4ToFloat(view, m16);
    _standardProg.setMat4("uModelView", m16);
    mat4ToFloat(proj, m16);
    _standardProg.setMat4("uProj", m16);
    float n9[9];
    normalMatrix3(view, n9);
    _standardProg.setMat3("uNormalMatrix", n9);
    // World light directions into view space
    Quat inv{-_camQuat.x, -_camQuat.y, -_camQuat.z, _camQuat.w};
    Vec3 l1 = inv.rotate(Vec3{80, 120, 60}.normalized());
    Vec3 l2 = inv.rotate(Vec3{-60, -20, -80}.normalized());
    _standardProg.set3f("uLightDir1", (float)l1.x, (float)l1.y, (float)l1.z);
    _standardProg.set3f("uLightDir2", (float)l2.x, (float)l2.y, (float)l2.z);
    _standardProg.set1i("uIsOrtho", _isPerspective ? 0 : 1);
  }
  _mesh.draw(GL_TRIANGLES);
}

void Viewer::drawLines(const LineBatch& b, const Mat4& viewProj,
                       bool depthTest, const Mat4* model) {
  if (b.mesh.empty()) return;
  _flatProg.use();
  Mat4 mvp = model ? viewProj.multiplied(*model) : viewProj;
  float m16[16];
  mat4ToFloat(mvp, m16);
  _flatProg.setMat4("uMVP", m16);
  _flatProg.set3f("uColor", b.color[0], b.color[1], b.color[2]);
  _flatProg.set1f("uOpacity", b.opacity);
  _flatProg.set1i("uToneMap", b.toneMapped ? 1 : 0);
  if (depthTest) glEnable(GL_DEPTH_TEST);
  else glDisable(GL_DEPTH_TEST);
  if (b.opacity < 1.0f) glEnable(GL_BLEND);
  else glDisable(GL_BLEND);
  b.mesh.draw(GL_LINES);
}

void Viewer::drawBasicMesh(const GlMesh& mesh, const float color[3],
                           float opacity, const Mat4& viewProj, bool depthTest,
                           bool polyOffset, float offsetFactor, bool lambert,
                           const Mat4& /*view*/) {
  if (mesh.empty()) return;
  ShaderProgram& prog = lambert ? _lambertProg : _flatProg;
  prog.use();
  float m16[16];
  mat4ToFloat(viewProj, m16);
  prog.setMat4("uMVP", m16);
  prog.set3f("uColor", color[0], color[1], color[2]);
  prog.set1f("uOpacity", opacity);
  if (!lambert) prog.set1i("uToneMap", 1);
  if (depthTest) glEnable(GL_DEPTH_TEST);
  else glDisable(GL_DEPTH_TEST);
  if (opacity < 1.0f) glEnable(GL_BLEND);
  else glDisable(GL_BLEND);
  if (polyOffset) {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(offsetFactor, offsetFactor);
  } else {
    glDisable(GL_POLYGON_OFFSET_FILL);
  }
  mesh.draw(GL_TRIANGLES);
  glDisable(GL_POLYGON_OFFSET_FILL);
}

void Viewer::drawFatLines(const FatLines& lines, const float color[3],
                          float opacity, float linewidth, const Mat4& view,
                          const Mat4& proj, bool depthTest,
                          bool polygonOffset) {
  if (lines.segCount == 0 || lines.vao == 0) return;
  _fatLineProg.use();
  float m16[16];
  mat4ToFloat(view, m16);
  _fatLineProg.setMat4("uModelView", m16);
  mat4ToFloat(proj, m16);
  _fatLineProg.setMat4("uProj", m16);
  _fatLineProg.set2f("uResolution", (float)_width, (float)_height);
  _fatLineProg.set1f("uLinewidth", linewidth);
  _fatLineProg.set3f("uColor", color[0], color[1], color[2]);
  _fatLineProg.set1f("uOpacity", opacity);
  if (depthTest) glEnable(GL_DEPTH_TEST);
  else glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND); // LineMaterial is always transparent
  if (polygonOffset) {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2, -2);
  } else {
    glDisable(GL_POLYGON_OFFSET_FILL);
  }
  glBindVertexArray(lines.vao);
  glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)lines.segCount);
  glBindVertexArray(0);
  glDisable(GL_POLYGON_OFFSET_FILL);
}

void Viewer::drawSprite(const Sprite& s, const Mat4& view, const Mat4& proj) {
  _spriteProg.use();
  float m16[16];
  mat4ToFloat(view, m16);
  _spriteProg.setMat4("uView", m16);
  mat4ToFloat(proj, m16);
  _spriteProg.setMat4("uProj", m16);
  _spriteProg.set3f("uCenter", (float)s.pos.x, (float)s.pos.y, (float)s.pos.z);
  _spriteProg.set2f("uScale", (float)s.sx, (float)s.sy);
  _spriteProg.set1i("uTex", 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, s.tex);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE); // sprite labels don't stamp the depth buffer
  glEnable(GL_BLEND);
  _spriteQuad.draw(GL_TRIANGLE_STRIP);
  glDepthMask(GL_TRUE);
}

void Viewer::drawFlatQuad(const FlatQuad& q, const Mat4& viewProj) {
  _texQuadProg.use();
  Mat4 mvp = viewProj.multiplied(q.model);
  float m16[16];
  mat4ToFloat(mvp, m16);
  _texQuadProg.setMat4("uMVP", m16);
  _texQuadProg.set1i("uTex", 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, q.tex);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  _labelQuad.draw(GL_TRIANGLE_STRIP);
  glDepthMask(GL_TRUE);
}

void Viewer::render() {
  _needsRender = false;
  glViewport(0, 0, _width, _height);
  glClearColor(_bg[0], _bg[1], _bg[2], 1.0f);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL); // three.js default LessEqualDepth
  glDepthMask(GL_TRUE);
  glDisable(GL_CULL_FACE); // materials are DoubleSide throughout
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  Mat4 view = viewMatrix();
  Mat4 proj = projectionMatrix();
  Mat4 viewProj = proj.multiplied(view);

  // Grid (opaque, toneMapped:false) at the model's ground level
  Mat4 gridModel = makeTranslation({0, 0, _gridZ});
  drawLines(_gridMajor, viewProj, true, &gridModel);
  drawLines(_gridMinor, viewProj, true, &gridModel);

  // Base mesh
  drawMesh(view, proj);

  // Exclusion overlay (Lambert, polygon offset -1)
  if (!_exclusionMesh.empty())
    drawBasicMesh(_exclusionMesh, _exclColor, _exclOpacity, viewProj, true,
                  true, -1, true, view);

  // Axis shafts + dimension lines (transparent thin lines)
  for (const auto& b : _axisLines) drawLines(b, viewProj, true);

  // Axis cones (opaque flat color)
  for (const auto& [mesh, hex] : _axisCones) {
    float c[3];
    hexToLinear(hex, c);
    drawBasicMesh(*mesh, c, 1.0f, viewProj, true, false, 0, false, view);
  }

  // Diagnostic face overlays
  for (const auto& df : _diagFaces)
    drawBasicMesh(df->mesh, df->color, df->opacity, viewProj, !df->xray,
                  !df->xray, -1, false, view);

  // Bucket-fill hover preview (yellow, 0.45, offset -2)
  if (!_hoverMesh.empty())
    drawBasicMesh(_hoverMesh, _hoverColor, 0.45f, viewProj, true, true, -2,
                  false, view);

  // Wireframe overlay (white 0.65, linewidth 1.2, offset -2)
  if (_wireVisible && _wireframe.segCount) {
    static const float white[3] = {1, 1, 1};
    drawFatLines(_wireframe, white, 0.65f, 1.2f, view, proj, true, true);
  }

  // Labels (depth-test on, depth-write off, drawn after the wireframe)
  for (const auto& q : _flatQuads) drawFlatQuad(q, viewProj);
  for (const auto& s : _sprites) drawSprite(s, view, proj);

  // Diagnostic edges (linewidth 3, no depth test)
  if (_diagEdges.segCount)
    drawFatLines(_diagEdges, _diagEdgeColor, 1.0f, 3.0f, view, proj, false,
                 false);

  // Ctrl-line paint preview (depthTest false, renderOrder 999)
  if (_shiftLineVisible && !_shiftLine.mesh.empty())
    drawLines(_shiftLine, viewProj, false);

  // Orbit pivot marker (red, no depth test)
  if (_pivotVisible) {
    Mat4 model = makeTranslation(_pivotPos)
                     .multiplied(Mat4::makeScale(_pivotScale, _pivotScale,
                                                 _pivotScale));
    float c[3];
    hexToLinear(0xff2222, c);
    drawBasicMesh(_pivotSphere, c, 1.0f, viewProj.multiplied(model), false,
                  false, 0, false, view);
  }

  // Rotation gizmo rings (no depth test, opacity 0.85)
  if (_gizmoVisible) {
    Mat4 model = makeTranslation(_gizmoCenter)
                     .multiplied(Mat4::makeScale(_gizmoScale, _gizmoScale,
                                                 _gizmoScale));
    Mat4 mvp = viewProj.multiplied(model);
    for (int axis = 0; axis < 3; axis++) {
      float c[3];
      hexToLinear(axis == _gizmoHoverAxis ? kGizmoHoverColors[axis]
                                          : kGizmoColors[axis],
                  c);
      drawBasicMesh(_gizmoRing[axis], c, 0.85f, mvp, false, false, 0, false,
                    view);
    }
  }

  // Restore a sane default state for the host (ImGui does its own setup)
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
}

} // namespace render
