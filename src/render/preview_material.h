// Port of reference/js/previewMaterial.js — the bump-only / GPU-displacement
// preview shader. The shared GLSL block (UV projection modes, seam blending,
// cap blending, masking) is ported near-verbatim; three.js built-ins
// (attributes, matrices) become explicit uniforms/locations.
//
// Fidelity note: three.js applies NO tone mapping or sRGB encode to custom
// ShaderMaterials, so this program writes raw colors — do not render it with
// GL_FRAMEBUFFER_SRGB enabled.
#pragma once

#include <optional>

#include "core/geometry.h"
#include "render/gl_util.h"
#include "render/math3d.h"

namespace render {

// Vertex attribute locations for meshes drawn with this material.
enum PreviewAttrib : GLuint {
  ATTR_POSITION = 0,
  ATTR_NORMAL = 1,
  ATTR_SMOOTH_NORMAL = 2,
  ATTR_FACE_NORMAL = 3,
  ATTR_FACE_MASK = 4,
  ATTR_BOUNDARY_FALLOFF = 5,
  ATTR_BOUNDARY_MASK_TYPE = 6,
};

// Mirror of the JS settings object consumed by updateMaterial().
struct PreviewParams {
  int mappingMode = 5;
  double scaleU = 1, scaleV = 1;
  double amplitude = 1;
  double offsetU = 0, offsetV = 0;
  double rotation = 0; // degrees
  core::Bounds bounds;
  bool hasBounds = false;
  std::optional<double> cylinderCenterX, cylinderCenterY, cylinderRadius;
  double bottomAngleLimit = 5;
  double topAngleLimit = 0;
  double mappingBlend = 0;
  double seamBandWidth = 0.35;
  double capAngle = 20;
  bool symmetricDisplacement = false;
  bool noDownwardZ = false;
  bool useDisplacement = false;
  double textureAspectU = 1, textureAspectV = 1;
  double boundaryFalloff = 0;
};

class PreviewMaterial {
public:
  bool init(); // build shader + fallback textures
  void destroy();

  // Replace the boundary-edge data texture (2 texels per edge, xyz in rgb).
  // edgeCount 0 clears back to the fallback.
  void setBoundaryEdges(const float* texels4f, int edgeCount);

  // displacementTex 0 → grey fallback (matches createFallbackTexture()).
  void bind(const PreviewParams& p, GLuint displacementTex,
            const Mat4& modelView, const Mat4& projection);

  ShaderProgram& program() { return _prog; }

private:
  ShaderProgram _prog;
  GLuint _fallbackTex = 0;
  GLuint _boundaryTex = 0;
  GLuint _fallbackBoundaryTex = 0;
  int _boundaryEdgeCount = 0;
  float _boundaryTexWidth = 1;
};

} // namespace render
