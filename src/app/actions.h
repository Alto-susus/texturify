// ModelSession — port of main.js's interaction layer: the current model +
// pose transform, exclusion painting (single / radius-BFS / bucket, with
// Shift-erase and Ctrl-line), place-on-face, rotate mode (inputs+gizmo), and
// precision masking (local subdivision under the radius brush). The cylinder
// axis silhouette panel itself lives in ui/cylinder_panel.{h,cpp} +
// app/cylinder_silhouette.{h,cpp}; it reads geometry()/bounds()/
// faceNormalAttr() below and writes settings.cylinderCenterX/Y/Radius
// directly (no ModelSession involvement needed — those settings are pure
// UV-projection state, not mesh topology).
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "app/app_state.h"
#include "app/masking.h"
#include "app/preview_attributes.h"
#include "core/exclusion.h"
#include "core/geometry.h"
#include "core/mesh_validation.h"
#include "render/math3d.h"
#include "render/viewer.h"

namespace app {

class ModelSession {
public:
  ModelSession(AppState& state, render::Viewer& viewer)
      : _st(state), _viewer(viewer) {}

  // Fresh model load: rebuilds bounds/adjacency/attributes, clears
  // exclusions and pose, fits the camera (viewer.loadGeometry).
  void setGeometry(core::Geometry geo, const std::string& name);
  const core::Geometry& geometry() const { return _geo; }
  const core::Bounds& bounds() const { return _bounds; }
  size_t triangleCount() const { return _geo.positions.size() / 9; }
  // Base-mesh per-vertex face-normal buffer (cylinder-axis autofit reads the
  // z-component to skip cap-like triangles, matching autoFitCylinderAxis()).
  const std::vector<float>& faceNormalAttr() const { return _faceNormalAttr; }
  // Bumped whenever the base mesh topology is replaced wholesale (fresh
  // load, rotate finalize, place-on-face) — the cylinder silhouette panel's
  // cached rasterization is only rebuilt when this changes, mirroring
  // main.js's `_cylSilhouetteGeometry === currentGeometry` identity check.
  int geometryEpoch() const { return _geoEpoch; }

  // Pose transform accumulated by rotate / place-on-face (undone on export).
  const render::Quat& poseRot() const { return _poseRot; }
  const core::Vec3& poseTrans() const { return _poseTrans; }

  // Per-frame: react to UI state diffs (tool/mode switches) and modifiers.
  void update(bool shiftDown, bool ctrlDown);

  // Pointer events in viewport-relative viewer pixels. onPointerDown returns
  // true when consumed (painting stroke / place-on-face click) — the host
  // must then skip the viewer's own pointer-down (no orbit).
  bool onPointerDown(double x, double y, int button);
  void onPointerMove(double x, double y);
  void onPointerUp(int button);

  // UI actions
  void applyRotation(double xDeg, double yDeg, double zDeg); // Euler XYZ delta
  void resetRotation();
  void setRotateMode(bool active); // gizmo toggle
  void setPlaceOnFace(bool active);
  void clearExclusions();
  const std::unordered_set<int32_t>& excludedFaces() const {
    return _excludedFaces;
  }
  // Undo/redo mask capture (main.js's _collectCurrentMask): projects live
  // precision-space selections back to base face indices when precision
  // masking is active and has selections, else returns the base set as-is.
  std::vector<int32_t> collectMaskFaces() const;
  // Seeds the base-mesh exclusion set (e.g. post-bake "mask just-baked faces"
  // — main.js's `adoptBakedGeometry(..., {preExcludedFaces})`). Call right
  // after setGeometry() on the same (freshly baked) geometry.
  void seedExcludedFaces(const std::vector<int32_t>& faces);
  // Undo/redo mask restore (main.js's _restoreMask): sets selection-mode and
  // clears both face sets synchronously — unlike the UI toggle, which only
  // takes effect on the next update() diff — then the caller reseeds via
  // seedExcludedFaces() in the same call, matching _restoreMask's flip-then-
  // reseed order without a frame of lag.
  void setSelectionModeImmediate(bool invert);

  // updateFaceMask(): recompute mask attributes (falloff only when the tool
  // is inactive and dirty, like the JS) and re-upload mesh attributes.
  void updateFaceMask();
  void markFalloffDirty() { _falloffDirty = true; }
  const MaskAttributes& maskAttributes() const { return _maskAttrs; }

  // Precision masking: locally subdivides the mesh (fast/preview mode) to
  // give the radius brush finer granularity than the base topology allows.
  // setPrecisionMasking(true) builds it; refreshPrecisionMesh() rebuilds it
  // at the current brush size (bound to the UI's "Refresh" button, shown
  // when checkPrecisionOutdated() flags st.precisionOutdated).
  void setPrecisionMasking(bool enable);
  void refreshPrecisionMesh();
  void checkPrecisionOutdated(); // call after brushRadius changes

  // 3D displacement preview: subdivides the current geometry to a moderate
  // resolution (subdivide -> [regularize -> re-subdivide]) and switches the
  // viewer to that mesh with vertex-shader displacement, so amplitude/scale
  // changes show real per-texel bump geometry instead of the coarse base
  // mesh's few vertices sliding as flat planes. Requires a loaded texture;
  // reverts st.displacementPreview3D to false (leaving the base mesh shown
  // in bump-only mode) if there's no model/texture yet, mirroring
  // toggleDisplacementPreview()'s early-out in main.js.
  void setDisplacementPreview(bool enable);

  // Fired after geometry-level changes (rotate finalize, place-on-face):
  // bounds changed, preview params need a refresh.
  std::function<void()> onGeometryChanged;
  // Fired when mask/preview attributes changed (repaint preview uniforms).
  std::function<void()> onMaskChanged;

  // ── Mesh diagnostics ──────────────────────────────────────────────────────
  // Fast checks (open/non-manifold edges, shell count) rerun automatically
  // from pushGeometry()'s fullReload path — see runFastDiagnosticsInternal().
  // Advanced checks (intersecting/overlapping triangles) are expensive, so
  // they're run by the caller (main.cpp's app::DiagnosticsRunner, on a
  // background thread) and reported back here once complete.
  void setAdvancedDiag(core::ExpensiveDiagnostics diag);
  void clearAdvancedDiag();
  // Show/hide the overlay for one finding kind (main.js's
  // toggleDiagHighlight/clearDiagHighlight) — re-toggling the active kind
  // hides it again.
  void toggleDiagHighlight(DiagHighlight kind);
  void clearDiagHighlight();

private:
  enum class Tool { None, Brush, Bucket };

  void pushGeometry(bool fullReload);
  MaskSettings maskSettings() const;
  void refreshExclusionOverlay();
  render::RaycastHit frontHit(double x, double y) const {
    return _viewer.raycastFront(x, y);
  }
  core::Vec3 viewDirFor(const core::Vec3& hitPt) const;
  void paintAt(double x, double y);
  void paintSingleHit(const render::RaycastHit& hit);
  void paintLineBetween(const core::Vec3& from, const core::Vec3& to);
  void paintFace(int32_t triIdx);
  void updateBrushCursor(double x, double y);
  void updateBrushHover(double x, double y);
  void updateBucketHover(double x, double y);
  void updatePlaceOnFaceHover(double x, double y);
  void setHoverFaces(const std::unordered_set<int32_t>& faces, uint32_t color);
  void clearHover();
  void clearShiftLine();
  void rotateGeometry(const render::Quat& q); // _rotateGeometry()
  void rotateFinalize();                      // _rotateFinalize()
  void rebuildAdjacency();
  void recomputeGeometryDerived(); // normals + bounds + attrs after edits
  // Recomputes fast diagnostics from _adj/_geo and mirrors the result into
  // AppState; called from pushGeometry() whenever fullReload is true (main.js
  // only ever calls updateMeshDiagnostics() at those same full-reload sites).
  void runFastDiagnosticsInternal();

  // excl-brush-radius-slider's value is a diameter (st.brushRadius); this is
  // the actual radius used everywhere geometrically (main.js's `brushRadius`
  // after the /2 the slider's 'input' handler applies).
  double brushRadiusActual() const { return _st.brushRadius * 0.5; }

  // Precision masking internals (main.js precisionMasking* globals)
  double computePrecisionEdgeLength(double brushDiameter) const;
  void deactivatePrecisionMasking();
  bool usingPrecision() const { return _precisionActive; }
  // 3D displacement-preview internals (main.js dispPreview* globals). Mutually
  // exclusive with precision masking — each deactivates the other on entry —
  // so it's safe to check this first in the active* accessors below.
  void activateDisplacementPreview();
  void deactivateDisplacementPreview();
  const core::Geometry& activeGeo() const {
    if (_dispPreviewActive) return _dispPreviewGeo;
    return usingPrecision() ? _precisionGeo : _geo;
  }
  const core::AdjacencyData& activeAdj() const {
    return usingPrecision() ? _precisionAdj : _adj;
  }
  const std::vector<float>& activeFaceNormalAttr() const {
    if (_dispPreviewActive) return _dispPreviewFaceNormalAttr;
    return usingPrecision() ? _precisionFaceNormalAttr : _faceNormalAttr;
  }
  const std::vector<float>& activeSmoothNormalAttr() const {
    if (_dispPreviewActive) return _dispPreviewSmoothNormalAttr;
    return usingPrecision() ? _precisionSmoothNormalAttr : _smoothNormalAttr;
  }
  std::unordered_set<int32_t>& activeExcludedFaces() {
    if (_dispPreviewActive) return _dispPreviewExcludedFaces;
    return usingPrecision() ? _precisionExcludedFaces : _excludedFaces;
  }
  size_t activeTriCount() const { return activeGeo().positions.size() / 9; }
  // pickTriangle(): raycast hit mapped back to BASE face-index space — bucket
  // fill always operates on base adjacency even while precision is active.
  int32_t pickBaseFace(double x, double y) const;

  AppState& _st;
  render::Viewer& _viewer;

  core::Geometry _geo;
  core::Bounds _bounds;
  core::AdjacencyData _adj;
  std::vector<float> _faceNormalAttr;   // addFaceNormals()
  std::vector<float> _smoothNormalAttr; // addSmoothNormals()
  MaskAttributes _maskAttrs;
  bool _falloffDirty = true;

  std::unordered_set<int32_t> _excludedFaces;
  render::Quat _poseRot;
  core::Vec3 _poseTrans{0, 0, 0};
  int _geoEpoch = 0;

  // Advanced-diagnostics face lists (kept here, not just as counts in
  // AppState, so toggleDiagHighlight() can rebuild the overlay geometry).
  std::vector<int32_t> _diagIntersectFaces, _diagOverlapFaces;

  // Precision masking state (main.js precisionGeometry/precisionParentMap/...)
  bool _precisionActive = false;
  bool _precisionBusy = false;
  core::Geometry _precisionGeo;
  std::vector<int32_t> _precisionParentMap; // precision face idx -> base idx
  double _precisionEdgeLength = 0;
  core::AdjacencyData _precisionAdj;
  std::vector<float> _precisionFaceNormalAttr, _precisionSmoothNormalAttr;
  std::unordered_set<int32_t> _precisionExcludedFaces;

  // 3D displacement-preview state (main.js dispPreviewGeometry/
  // dispPreviewParentMap). Unlike precision masking, this is never painted
  // into directly (the exclusion tool is force-exited on activation), so
  // there's no adjacency buffer — only what pushGeometry()/updateFaceMask()
  // need to render and mask the subdivided mesh.
  bool _dispPreviewActive = false;
  core::Geometry _dispPreviewGeo;
  std::vector<int32_t> _dispPreviewParentMap; // dispPreview face idx -> base idx
  std::vector<float> _dispPreviewFaceNormalAttr, _dispPreviewSmoothNormalAttr;
  std::unordered_set<int32_t> _dispPreviewExcludedFaces;

  // Painting state (mirrors main.js globals)
  Tool _tool = Tool::None;
  bool _brushIsRadius = false;
  bool _selectionMode = false;
  bool _eraseMode = false;
  bool _ctrlDown = false;
  bool _isPainting = false;
  std::optional<core::Vec3> _lastPaintHitPoint;
  int32_t _lastHoverTriIdx = -1;
  bool _shiftLineShown = false;
  double _cursorX = 0, _cursorY = 0;

  // Rotate mode
  bool _rotateActive = false;
  std::vector<float> _rotateOriginalPositions;
  render::Quat _rotatePoseRotSnapshot;
  core::Vec3 _rotatePoseTransSnapshot{0, 0, 0};
  double _rotateAngles[3] = {0, 0, 0};
};

} // namespace app
