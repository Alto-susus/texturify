#include "render/preview_material.h"

#include <algorithm>
#include <vector>

namespace render {

// ── GLSL — ported from previewMaterial.js sharedGLSL ─────────────────────────
// texture2D→texture, attribute/varying→in/out, gl_FragColor→outColor.
// Everything else (constants, branches, math) is kept verbatim.
static const char* kSharedGLSL = R"GLSL(
  uniform sampler2D displacementMap;
  uniform int       mappingMode;
  uniform vec2      scaleUV;
  uniform float     amplitude;
  uniform vec2      offsetUV;
  uniform float     rotation;
  uniform vec3      boundsMin;
  uniform vec3      boundsSize;
  uniform vec3      boundsCenter;
  uniform vec2      cylinderCenter;
  uniform float     cylinderRadius;
  uniform float     bottomAngleLimit;
  uniform float     topAngleLimit;
  uniform float     mappingBlend;
  uniform float     seamBandWidth;
  uniform float     capAngle;
  uniform int       symmetricDisplacement;
  uniform int       noDownwardZ;
  uniform int       useDisplacement;
  uniform vec2      textureAspect;

  const float PI     = 3.14159265358979;
  const float TWO_PI = 6.28318530717959;
  const float CUBIC_AXIS_EPSILON = 1e-4;

  int dominantCubicAxis(vec3 n) {
    vec3 absN = abs(n);
    if (absN.x >= absN.y - CUBIC_AXIS_EPSILON && absN.x >= absN.z - CUBIC_AXIS_EPSILON) return 0;
    if (absN.y >= absN.z - CUBIC_AXIS_EPSILON) return 1;
    return 2;
  }

  vec3 cubicBlendWeights(vec3 n) {
    vec3 absN = abs(n);
    int axis = dominantCubicAxis(n);
    float primary = axis == 0 ? absN.x : axis == 1 ? absN.y : absN.z;
    float secondary = axis == 0 ? max(absN.y, absN.z)
                    : axis == 1 ? max(absN.x, absN.z)
                                : max(absN.x, absN.y);

    // blend=0: hard one-hot for sharp seams. Do NOT also short-circuit at
    // primary≈secondary when blend>0 — the smooth branch produces 0.5/0.5
    // there, and short-circuiting to one-hot creates a single-fragment spike
    // wherever a fillet's smooth normal lands exactly on the 45° tie.
    if (mappingBlend < 0.001) {
      if (axis == 0) return vec3(1.0, 0.0, 0.0);
      if (axis == 1) return vec3(0.0, 1.0, 0.0);
      return vec3(0.0, 0.0, 1.0);
    }

    vec3 oneHot = axis == 0 ? vec3(1.0, 0.0, 0.0)
                : axis == 1 ? vec3(0.0, 1.0, 0.0)
                            : vec3(0.0, 0.0, 1.0);

    float seamWidth = max(seamBandWidth, CUBIC_AXIS_EPSILON * 2.0);
    float seamMixRaw = 1.0 - clamp((primary - secondary) / seamWidth, 0.0, 1.0);
    float seamMix = mappingBlend * seamMixRaw * seamMixRaw * (3.0 - 2.0 * seamMixRaw);
    if (seamMix <= 0.001) return oneHot;

    float power = 1.0 + (1.0 - seamMix) * 11.0;
    vec3 softWeights = pow(absN, vec3(power));
    softWeights /= dot(softWeights, vec3(1.0)) + 1e-6;

    vec3 blendedWeights = mix(oneHot, softWeights, seamMix);
    return blendedWeights / (dot(blendedWeights, vec3(1.0)) + 1e-6);
  }

  // Sample after applying scale + tiling (aspect-corrected)
  float sampleMap(vec2 rawUV) {
    vec2 uv = (rawUV * textureAspect) / scaleUV + offsetUV;
    float c = cos(rotation); float s = sin(rotation);
    uv -= 0.5;
    uv  = vec2(c * uv.x - s * uv.y, s * uv.x + c * uv.y);
    uv += 0.5;
    return texture(displacementMap, uv).r;
  }

  // Compute displacement height at a world-space point.
  // projN  = face-stable projection normal (for axis selection)
  // blendN = smooth / interpolated normal  (for blend weights)
  float computeHeightAtPoint(vec3 pos, vec3 projN, vec3 blendN) {
    vec3 rel = pos - boundsCenter;
    float maxDim = max(boundsSize.x, max(boundsSize.y, boundsSize.z));
    float md = max(maxDim, 1e-4);

    if (mappingMode == 0) {
      return sampleMap(vec2((pos.x - boundsMin.x) / md, (pos.y - boundsMin.y) / md));

    } else if (mappingMode == 1) {
      return sampleMap(vec2((pos.x - boundsMin.x) / md, (pos.z - boundsMin.z) / md));

    } else if (mappingMode == 2) {
      return sampleMap(vec2((pos.y - boundsMin.y) / md, (pos.z - boundsMin.z) / md));

    } else if (mappingMode == 3) {
      // Cylinder axis is +Z. Center XY and radius are user-controllable so
      // pie-slice / off-center parts can be projected without distortion.
      vec2 cylRel2 = pos.xy - cylinderCenter;
      float r = max(cylinderRadius, 1e-4);
      float C = TWO_PI * r;
      float u_cyl = atan(cylRel2.y, cylRel2.x) / TWO_PI + 0.5;
      float v_cyl = (pos.z - boundsMin.z) / C;

      // Seam smoothing: cross-fade between left-side and right-side texture
      // continuations at the atan2 wrap point.
      float seamBand = seamBandWidth * 0.1;
      float seamDist = min(u_cyl, 1.0 - u_cyl);
      float hSide;
      if (seamBand > 0.001 && seamDist < seamBand) {
        float d = u_cyl < 0.5 ? u_cyl : u_cyl - 1.0;
        float t = smoothstep(0.0, 1.0, (d + seamBand) / (2.0 * seamBand));
        float hLeft  = sampleMap(vec2(1.0 + d, v_cyl));
        float hRight = sampleMap(vec2(d, v_cyl));
        hSide = mix(hLeft, hRight, t);
      } else {
        hSide = sampleMap(vec2(u_cyl, v_cyl));
      }

      if (mappingBlend < 0.001) return hSide;
      float capThreshold = cos(radians(capAngle));
      float blendHalf = seamBandWidth * 0.5;
      float capW = smoothstep(capThreshold - blendHalf, capThreshold + blendHalf, abs(blendN.z));
      float hCap  = sampleMap(vec2(cylRel2.x / C + 0.5, cylRel2.y / C + 0.5));
      return mix(hSide, hCap, capW);

    } else if (mappingMode == 4) {
      float r     = length(rel);
      float phi   = acos(clamp(rel.z / max(r, 1e-4), -1.0, 1.0));
      float u_sph = atan(rel.y, rel.x) / TWO_PI + 0.5;
      float v_sph = phi / PI;

      // Seam smoothing: cross-fade at the atan2 wrap
      float seamBand = seamBandWidth * 0.1;
      float seamDist = min(u_sph, 1.0 - u_sph);
      if (seamBand > 0.001 && seamDist < seamBand) {
        float d = u_sph < 0.5 ? u_sph : u_sph - 1.0;
        float t = smoothstep(0.0, 1.0, (d + seamBand) / (2.0 * seamBand));
        float hLeft  = sampleMap(vec2(1.0 + d, v_sph));
        float hRight = sampleMap(vec2(d, v_sph));
        return mix(hLeft, hRight, t);
      }
      return sampleMap(vec2(u_sph, v_sph));

    } else if (mappingMode == 5) {
      vec3 blend = abs(projN);
      blend = pow(blend, vec3(4.0));
      blend /= dot(blend, vec3(1.0)) + 1e-4;
      // Flip U based on normal sign so opposite faces show correct (non-mirrored) text.
      float yzU = (pos.y - boundsMin.y) / md;
      if (projN.x < 0.0) yzU = -yzU;
      float xzU = (pos.x - boundsMin.x) / md;
      if (projN.y > 0.0) xzU = -xzU;
      float xyU = (pos.x - boundsMin.x) / md;
      if (projN.z < 0.0) xyU = -xyU;
      float hXY = sampleMap(vec2(xyU, (pos.y - boundsMin.y) / md));
      float hXZ = sampleMap(vec2(xzU, (pos.z - boundsMin.z) / md));
      float hYZ = sampleMap(vec2(yzU, (pos.z - boundsMin.z) / md));
      return hXY * blend.z + hXZ * blend.y + hYZ * blend.x;

    } else {
      // Flip U based on normal sign so opposite faces show correct (non-mirrored) text.
      float yzU = (pos.y - boundsMin.y) / md;
      if (projN.x < 0.0) yzU = -yzU;
      float xzU = (pos.x - boundsMin.x) / md;
      if (projN.y > 0.0) xzU = -xzU;
      float xyU = (pos.x - boundsMin.x) / md;
      if (projN.z < 0.0) xyU = -xyU;
      float hYZ = sampleMap(vec2(yzU, (pos.z - boundsMin.z) / md));
      float hXZ = sampleMap(vec2(xzU, (pos.z - boundsMin.z) / md));
      float hXY = sampleMap(vec2(xyU, (pos.y - boundsMin.y) / md));
      vec3 bN = blendN;
      vec3 absFaceN = abs(projN);
      float facePrimary = max(absFaceN.x, max(absFaceN.y, absFaceN.z));
      float faceSecondary = absFaceN.x + absFaceN.y + absFaceN.z - facePrimary
                          - min(absFaceN.x, min(absFaceN.y, absFaceN.z));
      if (facePrimary - faceSecondary <= CUBIC_AXIS_EPSILON) bN = projN;
      vec3 wts = cubicBlendWeights(bN);
      return hYZ * wts.x + hXZ * wts.y + hXY * wts.z;
    }
  }
)GLSL";

static const char* kVertexHead = R"GLSL(#version 330 core
  uniform mat4 modelViewMatrix;
  uniform mat4 projectionMatrix;
  uniform mat3 normalMatrix;
  layout(location = 0) in vec3  position;
  layout(location = 1) in vec3  normal;
  layout(location = 2) in vec3  smoothNormal;
  layout(location = 3) in vec3  faceNormal;
  layout(location = 4) in float faceMask;
  layout(location = 5) in float boundaryFalloffAttr;
  layout(location = 6) in float boundaryMaskTypeAttr;
)GLSL";

static const char* kVertexBody = R"GLSL(
  out vec3  vModelPos;    // ORIGINAL model-space position -> UV computation in fragment
  out vec3  vModelNormal; // model-space face normal       -> stable UV blending
  out vec3  vViewPos;     // view-space position (possibly displaced) -> TBN & specular
  out vec3  vNormal;      // view-space normal -> lighting
  out vec3  vSmoothNormal; // view-space smooth normal -> smooth shading on masked faces
  out float vFaceMask;    // combined mask (angle + user exclusion + boundary falloff)
  out float vUserMask;    // raw user-exclusion mask (0 = user-excluded, 1 = included)
  out float vMaskType;    // boundary mask type (0 = user mask, 1 = angle mask)

  void main() {
    vec3 safeN = length(normal) > 1e-6 ? normalize(normal) : vec3(0.0, 0.0, 1.0);
    // Use the true geometric face normal for angle masking so that
    // smooth/interpolated normals from subdivision don't cause mask bleeding.
    vec3 fN = length(faceNormal) > 1e-6 ? normalize(faceNormal) : safeN;
    vec3 pos = position;

    // Surface angle masking - hard per-face cutoff using flat face normal
    float surfaceAngle = degrees(acos(clamp(abs(fN.z), 0.0, 1.0)));
    float angleMask = 1.0;
    if (fN.z <  0.0 && bottomAngleLimit >= 1.0)
      angleMask = min(angleMask, surfaceAngle > bottomAngleLimit ? 1.0 : 0.0);
    if (fN.z >= 0.0 && topAngleLimit >= 1.0)
      angleMask = min(angleMask, surfaceAngle > topAngleLimit ? 1.0 : 0.0);
    float totalMask = angleMask * faceMask * boundaryFalloffAttr;
    vFaceMask = totalMask;
    vUserMask = faceMask;
    vMaskType = boundaryMaskTypeAttr;

    if (useDisplacement == 1) {
      float h = computeHeightAtPoint(position, safeN, safeN);
      if (symmetricDisplacement == 1) h = h - 0.5;
      h *= totalMask;

      // Displace along smooth normal so all copies of the same position
      // arrive at the same point (watertight, no cracks).
      vec3 sN = length(smoothNormal) > 1e-6 ? normalize(smoothNormal) : safeN;
      pos = position + sN * h * amplitude;
      // Overhang protection: never move a vertex below its original Z.
      if (noDownwardZ == 1 && pos.z < position.z) pos.z = position.z;
    }

    // Always pass the ORIGINAL position for UV computation in the fragment shader.
    vModelPos    = position;
    vModelNormal = fN;
    vec4 mvPos   = modelViewMatrix * vec4(pos, 1.0);
    vViewPos     = mvPos.xyz;
    vNormal      = normalize(normalMatrix * fN);
    vec3 sN2 = length(smoothNormal) > 1e-6 ? normalize(smoothNormal) : safeN;
    vSmoothNormal = normalize(normalMatrix * sN2);
    gl_Position  = projectionMatrix * mvPos;
  }
)GLSL";

static const char* kFragmentHead = R"GLSL(#version 330 core
)GLSL";

static const char* kFragmentBody = R"GLSL(
  uniform sampler2D boundaryEdgeTex;
  uniform int       boundaryEdgeCount;
  uniform float     boundaryEdgeTexWidth;
  uniform float     boundaryFalloffDist;

  in vec3  vModelPos;
  in vec3  vModelNormal;
  in vec3  vViewPos;
  in vec3  vNormal;
  in vec3  vSmoothNormal;
  in float vFaceMask;
  in float vUserMask;
  in float vMaskType;

  out vec4 outColor;

  // Fragment-only wrapper: compute face-stable projection normal via dFdx
  // then delegate to the shared height function.
  float getHeight() {
    vec3 _dpx = dFdx(vModelPos);
    vec3 _dpy = dFdy(vModelPos);
    vec3 _fN  = cross(_dpx, _dpy);
    vec3 PN   = length(_fN) > 1e-10 ? normalize(_fN) : vModelNormal;
    return computeHeightAtPoint(vModelPos, PN, vModelNormal);
  }

  void main() {
    // Flip normal for back faces so flipped-winding geometry still lights correctly.
    vec3 N = normalize(vNormal) * (gl_FrontFacing ? 1.0 : -1.0);
    float h = getHeight();
    if (symmetricDisplacement == 1) h = h - 0.5;

    // Bump mapping via screen-space height derivatives, computed on the RAW
    // (unmasked) height so 2x2 quads spanning mask boundaries don't spike.
    float dhx = dFdx(h);
    float dhy = dFdy(h);

    float maskBlend = vFaceMask;

    // Per-fragment boundary falloff for bump-only mode.
    if (useDisplacement == 0 && boundaryFalloffDist > 0.001 && boundaryEdgeCount > 0) {
      float minDist = boundaryFalloffDist;
      for (int i = 0; i < 64; i++) {
        if (i >= boundaryEdgeCount) break;
        float uA = (float(i * 2) + 0.5) / boundaryEdgeTexWidth;
        float uB = (float(i * 2 + 1) + 0.5) / boundaryEdgeTexWidth;
        vec3 ea = texture(boundaryEdgeTex, vec2(uA, 0.5)).xyz;
        vec3 eb = texture(boundaryEdgeTex, vec2(uB, 0.5)).xyz;
        vec3 ab = eb - ea;
        float abLen2 = dot(ab, ab);
        float t = clamp(dot(vModelPos - ea, ab) / max(abLen2, 1e-10), 0.0, 1.0);
        float d = length(vModelPos - (ea + t * ab));
        if (d < minDist) { minDist = d; if (d < 1e-4) break; }
      }
      maskBlend *= clamp(minDist / boundaryFalloffDist, 0.0, 1.0);
    }

    h *= maskBlend;
    dhx *= maskBlend;
    dhy *= maskBlend;

    vec3 dp1 = dFdx(vViewPos);
    vec3 dp2 = dFdy(vViewPos);

    vec3 T = dp1 - dot(dp1, N) * N;
    vec3 B = dp2 - dot(dp2, N) * N;
    float lenT = length(T);
    float lenB = length(B);
    T = lenT > 1e-5 ? T / lenT : vec3(1.0, 0.0, 0.0);
    B = lenB > 1e-5 ? B / lenB : vec3(0.0, 1.0, 0.0);

    // When vertex displacement is active, reduce bump strength: the macro shape
    // is already physical; bump only adds sub-vertex fine detail.
    float posScale = max(length(dp1) + length(dp2), 1e-6);
    float bumpStr  = useDisplacement == 1
      ? amplitude * 2.0 / posScale
      : amplitude * 6.0 / posScale;

    vec3 bumpVec = N - bumpStr * (dhx * T + dhy * B);
    vec3 bumpN = length(bumpVec) > 1e-6 ? normalize(bumpVec) : N;

    // Blend toward the smooth interpolated normal on masked faces so masked
    // areas get smooth shading instead of a faceted look.
    vec3 smoothN = normalize(vSmoothNormal) * (gl_FrontFacing ? 1.0 : -1.0);
    bumpN = mix(smoothN, bumpN, maskBlend);

    // Shading: identical lighting for all surfaces using the base color; mask
    // tinting is applied AFTER lighting as a colour blend.
    vec3 baseColor      = vec3(0.75, 0.16, 0.18);
    vec3 userMaskColor = vec3(0.85, 0.40, 0.15);
    vec3 angleMaskColor = vec3(0.45, 0.48, 0.50);

    vec3 L1 = normalize(vec3( 0.5,  0.8,  1.0));
    vec3 L2 = normalize(vec3(-0.5, -0.2, -0.6));
    vec3 V  = normalize(-vViewPos);

    float diff1 = max(dot(bumpN, L1), 0.0);
    float diff2 = max(dot(bumpN, L2), 0.0) * 0.35;

    vec3 H1   = normalize(L1 + V);
    float spec = pow(max(dot(bumpN, H1), 0.0), 64.0) * 0.60;

    vec3 litBase = baseColor * 0.55
                 + baseColor * diff1 * vec3(1.00, 0.96, 0.88) * 0.55
                 + baseColor * diff2 * vec3(0.80, 0.60, 0.50) * 0.15
                 + vec3(spec);

    float maskEffect = 1.0 - maskBlend; // 0 = fully textured, 1 = fully masked
    float effectiveMaskType = mix(vMaskType, 0.0, step(0.5, 1.0 - vUserMask));
    vec3 maskBase = mix(userMaskColor, angleMaskColor, effectiveMaskType);
    vec3 litMask = maskBase * 0.55
                 + maskBase * diff1 * vec3(1.00, 0.96, 0.88) * 0.55
                 + maskBase * diff2 * vec3(0.80, 0.60, 0.50) * 0.15
                 + vec3(spec);

    // Blend: 100% mask colour at the boundary, fading to 0% at falloff distance
    vec3 color = mix(litBase, litMask, maskEffect);

    outColor = vec4(color, 1.0);
  }
)GLSL";

bool PreviewMaterial::init() {
  std::string vs = std::string(kVertexHead) + kSharedGLSL + kVertexBody;
  std::string fs = std::string(kFragmentHead) + kSharedGLSL + kFragmentBody;
  if (!_prog.build(vs.c_str(), fs.c_str(), "previewMaterial")) return false;

  // createFallbackTexture(): 4×4 #808080, repeat wrap.
  uint8_t grey[4 * 4 * 4];
  for (int i = 0; i < 4 * 4; i++) {
    grey[i * 4] = grey[i * 4 + 1] = grey[i * 4 + 2] = 0x80;
    grey[i * 4 + 3] = 255;
  }
  _fallbackTex = createTextureRGBA(grey, 4, 4, /*repeat=*/true, true, false);

  // createFallbackDataTexture(): 1×1 float RGBA zeros.
  float zero[4] = {0, 0, 0, 0};
  _fallbackBoundaryTex = createDataTextureRGBA32F(zero, 1);
  return true;
}

void PreviewMaterial::destroy() {
  _prog.destroy();
  if (_fallbackTex) { glDeleteTextures(1, &_fallbackTex); _fallbackTex = 0; }
  if (_boundaryTex) { glDeleteTextures(1, &_boundaryTex); _boundaryTex = 0; }
  if (_fallbackBoundaryTex) {
    glDeleteTextures(1, &_fallbackBoundaryTex);
    _fallbackBoundaryTex = 0;
  }
}

void PreviewMaterial::setBoundaryEdges(const float* texels4f, int edgeCount) {
  if (_boundaryTex) { glDeleteTextures(1, &_boundaryTex); _boundaryTex = 0; }
  _boundaryEdgeCount = edgeCount;
  _boundaryTexWidth = 1;
  if (edgeCount > 0 && texels4f) {
    _boundaryTexWidth = (float)(edgeCount * 2);
    _boundaryTex = createDataTextureRGBA32F(texels4f, edgeCount * 2);
  }
}

void PreviewMaterial::bind(const PreviewParams& p, GLuint displacementTex,
                           const Mat4& modelView, const Mat4& projection) {
  _prog.use();

  float mv[16], proj[16], nm[9];
  mat4ToFloat(modelView, mv);
  mat4ToFloat(projection, proj);
  normalMatrix3(modelView, nm);
  _prog.setMat4("modelViewMatrix", mv);
  _prog.setMat4("projectionMatrix", proj);
  _prog.setMat3("normalMatrix", nm);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, displacementTex ? displacementTex : _fallbackTex);
  _prog.set1i("displacementMap", 0);
  glActiveTexture(GL_TEXTURE0 + 1);
  glBindTexture(GL_TEXTURE_2D, _boundaryTex ? _boundaryTex : _fallbackBoundaryTex);
  _prog.set1i("boundaryEdgeTex", 1);
  glActiveTexture(GL_TEXTURE0);

  _prog.set1i("mappingMode", p.mappingMode);
  _prog.set2f("scaleUV", (float)p.scaleU, (float)p.scaleV);
  _prog.set1f("amplitude", (float)p.amplitude);
  _prog.set2f("offsetUV", (float)p.offsetU, (float)p.offsetV);
  _prog.set1f("rotation", (float)(p.rotation * kPi / 180.0));

  const core::Bounds& b = p.bounds;
  if (p.hasBounds) {
    _prog.set3f("boundsMin", (float)b.min.x, (float)b.min.y, (float)b.min.z);
    _prog.set3f("boundsSize", (float)b.size.x, (float)b.size.y, (float)b.size.z);
    _prog.set3f("boundsCenter", (float)b.center.x, (float)b.center.y, (float)b.center.z);
  } else {
    _prog.set3f("boundsMin", 0, 0, 0);
    _prog.set3f("boundsSize", 1, 1, 1);
    _prog.set3f("boundsCenter", 0, 0, 0);
  }
  double cx = p.cylinderCenterX.value_or(b.center.x);
  double cy = p.cylinderCenterY.value_or(b.center.y);
  double cr = p.cylinderRadius.value_or(std::max(b.size.x, b.size.y) * 0.5);
  _prog.set2f("cylinderCenter", (float)cx, (float)cy);
  _prog.set1f("cylinderRadius", (float)cr);

  _prog.set1f("bottomAngleLimit", (float)p.bottomAngleLimit);
  _prog.set1f("topAngleLimit", (float)p.topAngleLimit);
  _prog.set1f("mappingBlend", (float)p.mappingBlend);
  _prog.set1f("seamBandWidth", (float)p.seamBandWidth);
  _prog.set1f("capAngle", (float)p.capAngle);
  _prog.set1i("symmetricDisplacement", p.symmetricDisplacement ? 1 : 0);
  _prog.set1i("noDownwardZ", p.noDownwardZ ? 1 : 0);
  _prog.set1i("useDisplacement", p.useDisplacement ? 1 : 0);
  _prog.set2f("textureAspect", (float)p.textureAspectU, (float)p.textureAspectV);
  _prog.set1i("boundaryEdgeCount", _boundaryEdgeCount);
  _prog.set1f("boundaryEdgeTexWidth", _boundaryTexWidth);
  _prog.set1f("boundaryFalloffDist", (float)p.boundaryFalloff);
}

} // namespace render
