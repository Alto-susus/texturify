// Port of reference/js/viewer.js — scene, Z-up ortho/perspective cameras,
// pivot-orbit controls, cursor-centric zoom, grid, axes indicator, dimension
// annotations, wireframe overlay, exclusion/hover/diagnostic overlays, orbit
// pivot marker, and the rotation gizmo.
//
// The host app forwards pointer/wheel events (viewport-relative pixels) and
// calls render() each frame; needsRender() gates redraws like the JS
// _needsRender flag.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/geometry.h"
#include "render/gl_util.h"
#include "render/math3d.h"
#include "render/preview_material.h"

namespace render {

struct RaycastHit {
  bool hit = false;
  Vec3 point;
  int32_t faceIndex = -1;
  double distance = 0;
};

// Optional per-vertex attributes for the preview material.
struct PreviewAttributes {
  const std::vector<float>* smoothNormal = nullptr;      // vec3
  const std::vector<float>* faceNormal = nullptr;        // vec3
  const std::vector<float>* faceMask = nullptr;          // float
  const std::vector<float>* boundaryFalloff = nullptr;   // float
  const std::vector<float>* boundaryMaskType = nullptr;  // float
};

class Viewer {
public:
  bool init(const std::string& assetDir);
  void destroy();

  // ── Scene content (mirrors viewer.js exports) ──────────────────────────
  // Full reload: replaces mesh, repositions grid/axes/dimensions, fits camera.
  void loadGeometry(const core::Geometry& geo,
                    const PreviewAttributes& attrs = {});
  // Swap geometry only; keeps camera/grid (setMeshGeometry).
  void setMeshGeometry(const core::Geometry& geo,
                       const PreviewAttributes& attrs = {});
  bool hasMesh() const { return _meshVerts > 0; }

  // Preview material state. usePreview=false → default standard material.
  void setPreviewEnabled(bool enabled) { _usePreview = enabled; _needsRender = true; }
  void setPreviewParams(const PreviewParams& p) { _previewParams = p; _needsRender = true; }
  void setDisplacementTexture(GLuint tex) { _displacementTex = tex; _needsRender = true; }
  PreviewMaterial& previewMaterial() { return _previewMat; }

  void setWireframe(bool enabled);
  void setExclusionOverlay(const core::Geometry* overlayGeo,
                           uint32_t color = 0xff6600, double opacity = 1.0);
  void setHoverPreview(const core::Geometry* overlayGeo,
                       uint32_t color = 0xffee00);
  void setDiagEdges(const std::vector<float>& segmentPositions,
                    uint32_t color = 0xff0000);
  void addDiagFaces(const core::Geometry& overlayGeo, uint32_t color,
                    double opacity = 0.6, bool xray = false);
  void clearDiagOverlays();

  void setProjection(bool perspective);
  bool isPerspective() const { return _isPerspective; }
  void setSceneBackground(uint32_t hexColor);

  // Rotation gizmo. onRotate(axis 0/1/2, deltaDegrees) during drag.
  void setRotationGizmo(bool visible,
                        std::function<void(int, double)> onRotate = nullptr);
  void updateRotationGizmo();
  bool isGizmoDragging() const { return _gizmoDragAxis >= 0; }

  // ── Input (viewport-relative pixels) ───────────────────────────────────
  // Returns true when the event was consumed (gizmo/orbit interaction).
  bool onPointerDown(double x, double y, int button);
  void onPointerMove(double x, double y);
  void onPointerUp(int button);
  void onScroll(double x, double y, double deltaY);
  // Suppress camera interaction (e.g. while painting exclusions).
  void setControlsEnabled(bool enabled) { _controlsEnabled = enabled; }

  RaycastHit raycast(double x, double y) const;
  // getFrontFaceHit(): first hit (by distance) whose geometric face normal
  // opposes the ray direction — DoubleSide materials return back-face hits of
  // adjacent triangles that can be marginally closer than the intended one.
  RaycastHit raycastFront(double x, double y) const;
  Ray screenRay(double x, double y) const;
  Vec3 cameraPosition() const { return _camPos; }
  // Camera world matrix column 0 (three.js setFromMatrixColumn(m, 0)).
  Vec3 cameraRight() const { return _camQuat.rotate({1, 0, 0}); }
  // World point → viewport pixels (three.js Vector3.project + rect math).
  void worldToScreen(const Vec3& p, double& xOut, double& yOut) const;

  // Ctrl-line paint preview (main.js _shiftLineMesh): 0x00ffaa line drawn
  // without depth testing. Pass visible=false to clear.
  void setShiftLine(bool visible, const Vec3& a = {}, const Vec3& b = {});

  // ── Frame ──────────────────────────────────────────────────────────────
  void resize(int widthPx, int heightPx);
  void render(); // draws at glViewport(0,0,w,h) of the bound framebuffer
  bool needsRender() const { return _needsRender; }
  void requestRender() { _needsRender = true; }

private:
  struct LineBatch { // one colored line-segment set
    GlMesh mesh;
    float color[3] = {1, 1, 1};
    float opacity = 1;
    bool toneMapped = true; // GridHelper's material sets toneMapped:false
  };
  struct Sprite { // billboard label
    GLuint tex = 0;
    Vec3 pos;
    double sx = 1, sy = 1;
  };
  struct FlatQuad { // ground-plane label
    GLuint tex = 0;
    Mat4 model;
  };
  struct DiagFaces {
    GlMesh mesh;
    float color[3];
    float opacity;
    bool xray;
  };

  void buildGrid();
  void buildAxesIndicator(double size, const Vec3& origin);
  void buildDimensions(const Vec3& boxMin, const Vec3& boxMax, double groundZ,
                       double scale);
  void buildWireframe();
  void buildGizmo();
  void updateGizmoScale(bool lock);
  int pickGizmoRing(double ndcX, double ndcY) const;
  bool gizmoPlaneAngle(double ndcX, double ndcY, int axis, double& out) const;

  Mat4 viewMatrix() const;
  Mat4 projectionMatrix() const;
  Vec3 unproject(double ndcX, double ndcY, double ndcZ) const;
  Ray screenRayNdcInternal(double ndcX, double ndcY) const;
  void lookAt(const Vec3& target);
  void fitCamera(const Vec3& center, double radius);

  // Screen-space thick line set (three.js LineSegments2 equivalent):
  // instanced quads, one instance per segment.
  struct FatLines {
    GLuint vao = 0, cornerVbo = 0, instVbo = 0;
    size_t segCount = 0;
    void build(const float* segments6f, size_t segCount);
    void destroy();
  };

  void drawMesh(const Mat4& view, const Mat4& proj);
  void drawLines(const LineBatch& b, const Mat4& viewProj, bool depthTest,
                 const Mat4* model = nullptr);
  void drawFatLines(const FatLines& lines, const float color[3], float opacity,
                    float linewidth, const Mat4& view, const Mat4& proj,
                    bool depthTest, bool polygonOffset);
  void drawBasicMesh(const GlMesh& mesh, const float color[3], float opacity,
                     const Mat4& viewProj, bool depthTest, bool polyOffset,
                     float offsetFactor, bool lambert, const Mat4& view);
  void drawSprite(const Sprite& s, const Mat4& view, const Mat4& proj);
  void drawFlatQuad(const FlatQuad& q, const Mat4& viewProj);

  // GL programs
  ShaderProgram _flatProg;      // uniform-color lines & basic meshes
  ShaderProgram _lambertProg;   // exclusion overlay (scene lights)
  ShaderProgram _standardProg;  // default MeshStandardMaterial look
  ShaderProgram _fatLineProg;   // screen-space thick lines (LineSegments2)
  ShaderProgram _spriteProg;    // billboard textured quads
  ShaderProgram _texQuadProg;   // world-space textured quads
  PreviewMaterial _previewMat;

  // Mesh
  GlMesh _mesh;
  size_t _meshVerts = 0;
  std::vector<float> _meshPositions; // model-space copy for raycasts
  bool _usePreview = false;
  PreviewParams _previewParams;
  GLuint _displacementTex = 0;

  // Scene helpers
  LineBatch _gridMajor, _gridMinor;
  double _gridZ = 0;
  std::vector<LineBatch> _axisLines;   // shafts + dimension lines/ticks
  std::vector<std::pair<GlMesh*, uint32_t>> _axisCones;
  std::vector<std::unique_ptr<GlMesh>> _ownedMeshes;
  std::vector<Sprite> _sprites;      // axis labels
  std::vector<FlatQuad> _flatQuads;  // dimension labels
  std::vector<GLuint> _ownedTextures;

  // Overlays
  FatLines _wireframe;
  bool _wireVisible = false;
  GlMesh _exclusionMesh;
  float _exclColor[3] = {1, 0.4f, 0};
  float _exclOpacity = 1;
  GlMesh _hoverMesh;
  float _hoverColor[3] = {1, 0.93f, 0};
  FatLines _diagEdges;
  float _diagEdgeColor[3] = {1, 0, 0};
  std::vector<std::unique_ptr<DiagFaces>> _diagFaces;
  LineBatch _shiftLine;
  bool _shiftLineVisible = false;

  // Shared unit quads: sprite corners (vec2) and textured label quad (pos+uv)
  GlMesh _spriteQuad, _labelQuad;

  // Pivot marker
  GlMesh _pivotSphere;
  bool _pivotVisible = false;
  Vec3 _pivotPos;
  double _pivotScale = 1;

  // Gizmo
  bool _gizmoVisible = false;
  std::function<void(int, double)> _gizmoCallback;
  GlMesh _gizmoRing[3];     // visible tori
  std::vector<float> _gizmoHitTris[3]; // fat hit tori (triangle soup, unit)
  Vec3 _gizmoCenter;
  double _gizmoScale = 1;
  bool _gizmoScaleLocked = false;
  int _gizmoDragAxis = -1;
  double _gizmoLastAngle = 0;
  int _gizmoHoverAxis = -1;

  // Camera state (both cameras share position/orientation semantics of JS)
  bool _isPerspective = false;
  Vec3 _camPos{120, -200, 100};
  Quat _camQuat;
  Vec3 _camUp{0, 0, 1};
  Vec3 _target{0, 0, 0};
  double _orthoHalfH = 150, _orthoZoom = 1;
  double _orthoNear = -10000, _orthoFar = 10000;
  double _perspFov = 50, _perspNear = 0.1, _perspFar = 20000;

  // Controls
  bool _controlsEnabled = true;
  bool _orbiting = false;
  bool _panning = false;
  Vec3 _orbitPivot;
  bool _hasLastPivot = false;
  Vec3 _lastPivot;
  double _lastX = 0, _lastY = 0;

  int _width = 1, _height = 1;
  float _bg[3] = {0x11 / 255.0f, 0x11 / 255.0f, 0x14 / 255.0f};
  bool _needsRender = true;
  std::string _assetDir;
};

} // namespace render
