#include "app/actions.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/pipeline_runner.h" // toRegularizeOpts
#include "core/regularize.h"
#include "core/subdivision.h"

namespace app {

using core::Vec3;
using render::Quat;
using render::RaycastHit;

namespace {
// Overlay mask vector from a face set (buildExclusionOverlayGeo input).
std::vector<uint8_t> setToMask(const std::unordered_set<int32_t>& faces,
                               size_t triCount) {
  std::vector<uint8_t> mask(triCount, 0);
  for (int32_t f : faces)
    if (f >= 0 && (size_t)f < triCount) mask[f] = 1;
  return mask;
}
// Same, but from an ordered face-index list (advanced-diagnostics results).
std::vector<uint8_t> listToMask(const std::vector<int32_t>& faces,
                                size_t triCount) {
  std::vector<uint8_t> mask(triCount, 0);
  for (int32_t f : faces)
    if (f >= 0 && (size_t)f < triCount) mask[f] = 1;
  return mask;
}
// SHELL_COLORS from main.js — evenly spaced hues, high saturation.
constexpr uint32_t kShellColors[] = {
    0xe6194b, 0x3cb44b, 0x4363d8, 0xf58231, 0x911eb4, 0x42d4f4, 0xf032e6,
    0xbfef45, 0xfabed4, 0xdcbeff, 0x9a6324, 0x800000, 0xaaffc3, 0x808000,
    0x000075, 0xa9a9a9};
} // namespace

// ── Geometry lifecycle ───────────────────────────────────────────────────────

void ModelSession::setGeometry(core::Geometry geo, const std::string& name) {
  _geo = std::move(geo);
  _st.meshName = name;
  _excludedFaces.clear();
  _poseRot = Quat{};
  _poseTrans = {0, 0, 0};
  _rotateActive = false;
  _st.placeOnFaceActive = false;
  _lastPaintHitPoint.reset();
  _lastHoverTriIdx = -1;
  _falloffDirty = true;
  // A fresh model invalidates any precision-masking refinement of the old one.
  _precisionActive = false;
  _precisionGeo = core::Geometry{};
  _precisionParentMap.clear();
  _precisionEdgeLength = 0;
  _precisionAdj = core::AdjacencyData{};
  _precisionFaceNormalAttr.clear();
  _precisionSmoothNormalAttr.clear();
  _precisionExcludedFaces.clear();
  _st.precisionMasking = false;
  _st.precisionStatusText.clear();
  _st.precisionOutdated = false;
  // A fresh model invalidates any displacement-preview refinement of the old
  // one too — without this, activeGeo() keeps returning the stale (differently
  // sized) preview mesh while recomputeGeometryDerived() below sizes the
  // mask/normal attributes to the NEW _geo, a buffer mismatch that reads as
  // all-zero mask attributes in the shader and renders the whole model in the
  // "excluded" orange color.
  _dispPreviewActive = false;
  _dispPreviewGeo = core::Geometry{};
  _dispPreviewParentMap.clear();
  _dispPreviewFaceNormalAttr.clear();
  _dispPreviewSmoothNormalAttr.clear();
  _dispPreviewExcludedFaces.clear();
  _st.displacementPreview3D = false;
  recomputeGeometryDerived();
  // Default edge length = diag/250 of the new model's bounding box, clamped
  // to [0.05, 5.0] — matches main.js's loadDefaultCube()/handleModelFile(),
  // both of which recompute this on every fresh geometry load (not just
  // Reset-to-defaults, which is the only place this port previously did it).
  // Without this, refineLength stayed stuck at the compiled-in 1.0 default
  // for every load, silently diverging from the reference app's actual
  // per-model subdivision resolution (e.g. a 50 mm cube should default to
  // 0.35 mm, not 1.0 mm).
  {
    const double diag = std::sqrt(_bounds.size.x * _bounds.size.x +
                                  _bounds.size.y * _bounds.size.y +
                                  _bounds.size.z * _bounds.size.z);
    if (diag > 0) {
      const double v = std::round(diag / 250.0 * 100.0) / 100.0;
      _st.settings.refineLength = std::max(0.05, std::min(5.0, v));
    }
  }
  pushGeometry(/*fullReload=*/true);
  if (onGeometryChanged) onGeometryChanged();
}

void ModelSession::recomputeGeometryDerived() {
  if (_geo.normals.size() != _geo.positions.size())
    _geo.normals = computeFaceNormals(_geo); // computeVertexNormals (flat)
  _bounds = core::computeBounds(_geo);
  rebuildAdjacency();
  _faceNormalAttr = computeFaceNormals(_geo);
  _smoothNormalAttr = computeSmoothNormals(_geo);
  _maskAttrs = computeMaskAttributes(_geo, _faceNormalAttr, _excludedFaces,
                                     _selectionMode, maskSettings(),
                                     /*computeFalloff=*/true);
  _falloffDirty = false;
  _st.meshTriangles = triangleCount();
  _st.meshBounds = _bounds;
  _st.excludedFaceCount = (int)_excludedFaces.size();
}

void ModelSession::rebuildAdjacency() { _adj = core::buildAdjacency(_geo); }

MaskSettings ModelSession::maskSettings() const {
  MaskSettings s;
  s.bottomAngleLimit = _st.settings.bottomAngleLimit;
  s.topAngleLimit = _st.settings.topAngleLimit;
  s.boundaryFalloff = _st.settings.boundaryFalloff;
  return s;
}

void ModelSession::pushGeometry(bool fullReload) {
  render::PreviewAttributes attrs;
  const std::vector<float>& sn = activeSmoothNormalAttr();
  const std::vector<float>& fn = activeFaceNormalAttr();
  attrs.smoothNormal = &sn;
  attrs.faceNormal = &fn;
  attrs.faceMask = &_maskAttrs.faceMask;
  attrs.boundaryFalloff = &_maskAttrs.boundaryFalloff;
  attrs.boundaryMaskType = &_maskAttrs.boundaryMaskType;
  const core::Geometry& g = activeGeo();
  if (fullReload) {
    _viewer.loadGeometry(g, attrs);
    _geoEpoch++;
    runFastDiagnosticsInternal();
  } else
    _viewer.setMeshGeometry(g, attrs);
  _viewer.previewMaterial().setBoundaryEdges(
      _maskAttrs.boundaryEdgeTexels.empty()
          ? nullptr
          : _maskAttrs.boundaryEdgeTexels.data(),
      _maskAttrs.boundaryEdgeCount);
}

void ModelSession::updateFaceMask() {
  // JS gate: falloff recompute is skipped while a masking tool is active.
  const bool computeFalloff = _tool == Tool::None && _falloffDirty;
  MaskAttributes fresh =
      computeMaskAttributes(activeGeo(), activeFaceNormalAttr(),
                            activeExcludedFaces(), _selectionMode,
                            maskSettings(), computeFalloff);
  if (!computeFalloff &&
      _maskAttrs.boundaryFalloff.size() == fresh.boundaryFalloff.size()) {
    // Keep the stale falloff data during a stroke, like the JS attributes.
    fresh.boundaryFalloff = _maskAttrs.boundaryFalloff;
    fresh.boundaryMaskType = _maskAttrs.boundaryMaskType;
    fresh.boundaryEdgeTexels = _maskAttrs.boundaryEdgeTexels;
    fresh.boundaryEdgeCount = _maskAttrs.boundaryEdgeCount;
  }
  _maskAttrs = std::move(fresh);
  if (computeFalloff) _falloffDirty = false;
  _st.excludedFaceCount = (int)activeExcludedFaces().size();
  pushGeometry(/*fullReload=*/false);
  if (onMaskChanged) onMaskChanged();
}

void ModelSession::refreshExclusionOverlay() {
  _falloffDirty = true;
  updateFaceMask();
}

void ModelSession::clearExclusions() {
  _excludedFaces.clear();
  _precisionExcludedFaces.clear();
  refreshExclusionOverlay();
}

void ModelSession::seedExcludedFaces(const std::vector<int32_t>& faces) {
  _excludedFaces.clear();
  const size_t triCount = triangleCount();
  for (int32_t f : faces)
    if (f >= 0 && (size_t)f < triCount) _excludedFaces.insert(f);
  refreshExclusionOverlay();
}

std::vector<int32_t> ModelSession::collectMaskFaces() const {
  std::vector<int32_t> out;
  if (_precisionActive && !_precisionParentMap.empty() && !_precisionExcludedFaces.empty()) {
    std::unordered_set<int32_t> baseSet;
    for (int32_t pf : _precisionExcludedFaces)
      if ((size_t)pf < _precisionParentMap.size()) baseSet.insert(_precisionParentMap[pf]);
    out.assign(baseSet.begin(), baseSet.end());
  } else {
    out.assign(_excludedFaces.begin(), _excludedFaces.end());
  }
  return out;
}

void ModelSession::setSelectionModeImmediate(bool invert) {
  _st.brushMode = invert ? BrushMode::Include : BrushMode::Exclude;
  if (_selectionMode != invert) {
    _selectionMode = invert;
    _excludedFaces.clear();
    _precisionExcludedFaces.clear();
  }
}

// ── Mesh diagnostics ─────────────────────────────────────────────────────────

void ModelSession::runFastDiagnosticsInternal() {
  const core::FastDiagnostics d =
      core::runFastDiagnostics(_adj, (int64_t)triangleCount());
  _st.diagHasFast = true;
  _st.diagOpenEdges = d.openEdges;
  _st.diagNonManifoldEdges = d.nonManifoldEdges;
  _st.diagShellCount = d.shellCount;
  _st.diagDismissed = false;
  clearAdvancedDiag();
  clearDiagHighlight();
}

void ModelSession::setAdvancedDiag(core::ExpensiveDiagnostics diag) {
  _st.diagHasAdvanced = true;
  _st.diagAdvancedRunning = false;
  _st.diagIntersectingPairs = diag.intersectingPairs;
  _st.diagOverlappingPairs = diag.overlappingPairs;
  _diagIntersectFaces = std::move(diag.intersectFaces);
  _diagOverlapFaces = std::move(diag.overlapFaces);
}

void ModelSession::clearAdvancedDiag() {
  _st.diagHasAdvanced = false;
  _st.diagAdvancedRunning = false;
  _st.diagIntersectingPairs = 0;
  _st.diagOverlappingPairs = 0;
  _diagIntersectFaces.clear();
  _diagOverlapFaces.clear();
  if (_st.diagActiveHighlight == DiagHighlight::Intersects ||
      _st.diagActiveHighlight == DiagHighlight::Overlaps)
    clearDiagHighlight();
}

void ModelSession::clearDiagHighlight() {
  _viewer.clearDiagOverlays();
  _st.diagActiveHighlight = DiagHighlight::None;
}

void ModelSession::toggleDiagHighlight(DiagHighlight kind) {
  if (_st.diagActiveHighlight == kind) {
    clearDiagHighlight();
    return;
  }
  _viewer.clearDiagOverlays();
  _st.diagActiveHighlight = kind;
  if (_geo.positions.empty()) return;

  if (kind == DiagHighlight::OpenEdges || kind == DiagHighlight::NonManifold) {
    const core::EdgeHighlightPositions edges = core::getEdgePositions(_geo);
    const std::vector<float>& positions =
        kind == DiagHighlight::OpenEdges ? edges.open : edges.nonManifold;
    _viewer.setDiagEdges(positions, 0xff0000);
  } else if (kind == DiagHighlight::Shells) {
    const std::vector<uint32_t> shellIds =
        core::getShellAssignments(_adj, (int64_t)triangleCount());
    const size_t triCount = triangleCount();
    for (int64_t s = 0; s < _st.diagShellCount; s++) {
      core::Geometry overlay;
      for (size_t t = 0; t < triCount; t++) {
        if (shellIds[t] != (uint32_t)s) continue;
        const size_t src = t * 9;
        overlay.positions.insert(overlay.positions.end(),
                                 _geo.positions.begin() + src,
                                 _geo.positions.begin() + src + 9);
        if (!_geo.normals.empty())
          overlay.normals.insert(overlay.normals.end(),
                                 _geo.normals.begin() + src,
                                 _geo.normals.begin() + src + 9);
      }
      if (overlay.positions.empty()) continue;
      _viewer.addDiagFaces(overlay, kShellColors[s % 16], 0.55);
    }
  } else if (kind == DiagHighlight::Intersects && !_diagIntersectFaces.empty()) {
    core::Geometry overlay = core::buildExclusionOverlayGeo(
        _geo, listToMask(_diagIntersectFaces, triangleCount()));
    _viewer.addDiagFaces(overlay, 0xff0000, 0.7, true);
  } else if (kind == DiagHighlight::Overlaps && !_diagOverlapFaces.empty()) {
    core::Geometry overlay = core::buildExclusionOverlayGeo(
        _geo, listToMask(_diagOverlapFaces, triangleCount()));
    _viewer.addDiagFaces(overlay, 0xf59e0b, 0.7);
  }
}

// ── Per-frame state sync ─────────────────────────────────────────────────────

void ModelSession::update(bool shiftDown, bool ctrlDown) {
  const Tool tool = _st.paintingEnabled
                        ? (_st.brushTool == BrushTool::Bucket ? Tool::Bucket
                                                              : Tool::Brush)
                        : Tool::None;
  const bool brushIsRadius = _st.brushTool == BrushTool::Radius;
  const bool selectionMode = _st.brushMode == BrushMode::Include;

  // setSelectionMode(): switching semantics clears both painted sets
  if (selectionMode != _selectionMode) {
    _selectionMode = selectionMode;
    _excludedFaces.clear();
    _precisionExcludedFaces.clear();
    refreshExclusionOverlay();
  }

  // setExclusionTool()
  if (tool != _tool || brushIsRadius != _brushIsRadius) {
    // Precision masking only makes sense under the radius brush (its own
    // UI row is hidden otherwise); switching to Single deactivates it.
    if (!brushIsRadius && _precisionActive) deactivatePrecisionMasking();
    _tool = tool;
    _brushIsRadius = brushIsRadius;
    if (_tool != Tool::None) {
      if (_st.placeOnFaceActive) setPlaceOnFace(false);
      if (_rotateActive) {
        _st.rotateGizmo = false;
        setRotateMode(false);
      }
      // Exit 3D displacement preview when a masking tool is activated
      if (_st.displacementPreview3D) deactivateDisplacementPreview();
    }
    _lastHoverTriIdx = -1;
    clearHover();
    if (!(_tool == Tool::Brush && _brushIsRadius))
      _st.brushCursorVisible = false;
    if (_tool == Tool::None) {
      _isPainting = false;
      _viewer.setControlsEnabled(true);
      if (_falloffDirty) updateFaceMask(); // recompute falloff now
    }
  }

  checkPrecisionOutdated();

  _eraseMode = shiftDown && _tool != Tool::None;
  if (!ctrlDown && _ctrlDown) clearShiftLine(); // keyup Control
  _ctrlDown = ctrlDown;
}

// ── Pointer handling ─────────────────────────────────────────────────────────

bool ModelSession::onPointerDown(double x, double y, int button) {
  if (button != 0 || _geo.positions.empty()) return false;
  if (_viewer.isGizmoDragging()) return false;

  if (_st.placeOnFaceActive) {
    // Place-on-face always forces precision masking off first (setPlaceOnFace),
    // so the viewer's active mesh is always the base geometry here.
    RaycastHit hit = frontHit(x, y);
    if (hit.hit) {
      // handlePlaceOnFaceClick(): rotate the picked face down onto the bed
      const size_t b = (size_t)hit.faceIndex * 9;
      const auto& p = _geo.positions;
      Vec3 a{p[b], p[b + 1], p[b + 2]};
      Vec3 bb{p[b + 3], p[b + 4], p[b + 5]};
      Vec3 c{p[b + 6], p[b + 7], p[b + 8]};
      Vec3 n = (bb - a).cross(c - a);
      double len = n.length();
      if (len == 0) len = 1;
      n = n * (1.0 / len);
      Quat quat = Quat::fromUnitVectors(n, {0, 0, -1});

      auto& pos = _geo.positions;
      for (size_t i = 0; i + 2 < pos.size(); i += 3) {
        Vec3 v = quat.rotate({pos[i], pos[i + 1], pos[i + 2]});
        pos[i] = (float)v.x;
        pos[i + 1] = (float)v.y;
        pos[i + 2] = (float)v.z;
      }
      _poseRot = _poseRot.premultiplied(quat).normalized();
      _poseTrans = quat.rotate(_poseTrans);

      // Re-center
      core::Bounds bo = core::computeBounds(_geo);
      for (size_t i = 0; i + 2 < pos.size(); i += 3) {
        pos[i] = (float)(pos[i] - bo.center.x);
        pos[i + 1] = (float)(pos[i + 1] - bo.center.y);
        pos[i + 2] = (float)(pos[i + 2] - bo.center.z);
      }
      _poseTrans = _poseTrans - bo.center;

      // Recompute normals from scratch; cylinder axis settings are stale
      _geo.normals = computeFaceNormals(_geo);
      _st.settings.cylinderCenterX.reset();
      _st.settings.cylinderCenterY.reset();
      _st.settings.cylinderRadius.reset();
      if (_st.displacementPreview3D) deactivateDisplacementPreview();

      // Deactivate tools but keep excludedFaces (face indices are stable)
      _isPainting = false;
      _lastPaintHitPoint.reset();
      clearHover();

      recomputeGeometryDerived();
      // Update edge length for new bounds (loadGeometry default)
      const double diag = std::sqrt(_bounds.size.x * _bounds.size.x +
                                    _bounds.size.y * _bounds.size.y +
                                    _bounds.size.z * _bounds.size.z);
      double defaultEdge = diag / 300.0;
      defaultEdge = std::round(defaultEdge * 100.0) / 100.0; // toFixed(2)
      _st.settings.refineLength =
          std::max(0.05, std::min(5.0, defaultEdge));
      pushGeometry(/*fullReload=*/true);
      if (onGeometryChanged) onGeometryChanged();
    }
    setPlaceOnFace(false); // exit place-on-face mode
    return true;
  }

  if (_tool == Tool::None) return false;
  if (_precisionBusy) return false; // JS: block painting while refining

  if (_tool == Tool::Bucket) {
    _lastHoverTriIdx = -1;
    clearHover();
    // pickTriangle(): bucket fill always walks the BASE adjacency, even
    // while precision masking is active — the projection into
    // precisionExcludedFaces happens after the fill.
    int32_t baseIdx = pickBaseFace(x, y);
    if (baseIdx >= 0) {
      std::vector<int32_t> filled =
          core::bucketFill(baseIdx, _adj, _st.bucketAngle);
      for (int32_t t : filled) {
        if (_eraseMode) _excludedFaces.erase(t);
        else _excludedFaces.insert(t);
      }
      if (usingPrecision() && !_precisionParentMap.empty()) {
        std::unordered_set<int32_t> filledSet(filled.begin(), filled.end());
        for (size_t i = 0; i < _precisionParentMap.size(); i++) {
          if (filledSet.count(_precisionParentMap[i])) {
            if (_eraseMode) _precisionExcludedFaces.erase((int32_t)i);
            else _precisionExcludedFaces.insert((int32_t)i);
          }
        }
      }
      refreshExclusionOverlay();
    }
    // JS never disables orbit controls for the bucket — a click can still
    // start a drag-orbit; the host falls through to the viewer.
    return false;
  }

  // Brush: only start painting when we actually hit the mesh
  RaycastHit hit = frontHit(x, y);
  if (!hit.hit) return false; // miss → let orbit handle the drag
  _viewer.setControlsEnabled(false);
  _isPainting = true;
  _lastHoverTriIdx = -1;
  clearHover();
  paintAt(x, y);
  return true;
}

void ModelSession::onPointerMove(double x, double y) {
  _cursorX = x;
  _cursorY = y;
  if (_geo.positions.empty()) return;

  if (_isPainting && _tool == Tool::Brush) {
    paintAt(x, y);
    updateBrushCursor(x, y);
    return;
  }
  if (_st.placeOnFaceActive) {
    updatePlaceOnFaceHover(x, y);
    return;
  }
  if (_tool == Tool::Brush) {
    updateBrushCursor(x, y);
    if (!_isPainting) updateBrushHover(x, y);
    // Ctrl-line preview
    if (_ctrlDown && _lastPaintHitPoint) {
      RaycastHit hit = frontHit(x, y);
      if (hit.hit) {
        _viewer.setShiftLine(true, *_lastPaintHitPoint, hit.point);
        _shiftLineShown = true;
      } else {
        clearShiftLine();
      }
    } else {
      clearShiftLine();
    }
  } else if (_tool == Tool::Bucket && !_isPainting) {
    updateBucketHover(x, y);
  }
}

void ModelSession::onPointerUp(int button) {
  if (button != 0 || !_isPainting) return;
  _isPainting = false;
  _viewer.setControlsEnabled(true);
}

// ── Painting ─────────────────────────────────────────────────────────────────

Vec3 ModelSession::viewDirFor(const Vec3& hitPt) const {
  return (hitPt - _viewer.cameraPosition()).normalized();
}

void ModelSession::paintFace(int32_t triIdx) {
  std::unordered_set<int32_t>& faces = activeExcludedFaces();
  if (_eraseMode) faces.erase(triIdx);
  else faces.insert(triIdx);
}

void ModelSession::paintSingleHit(const RaycastHit& hit) {
  if (_brushIsRadius) {
    const double r = brushRadiusActual();
    const double r2 = r * r;
    bfsBrushSelect(hit.faceIndex, hit.point, r2, viewDirFor(hit.point),
                   activeGeo(), activeAdj(),
                   [this](int32_t t) { paintFace(t); });
  } else {
    paintFace(hit.faceIndex);
  }
}

void ModelSession::paintLineBetween(const Vec3& from, const Vec3& to) {
  const double dist = (to - from).length();
  const double step =
      _brushIsRadius ? std::max(brushRadiusActual() * 0.5, 0.1) : 0.5;
  const int steps = std::max((int)std::ceil(dist / step), 1);
  for (int i = 0; i <= steps; i++) {
    const double t = (double)i / steps;
    Vec3 pt = from + (to - from) * t;
    double sx, sy;
    _viewer.worldToScreen(pt, sx, sy);
    RaycastHit hit = frontHit(sx, sy);
    if (hit.hit) paintSingleHit(hit);
  }
}

void ModelSession::paintAt(double x, double y) {
  RaycastHit hit = frontHit(x, y);
  if (!hit.hit) return;
  if (_ctrlDown && _lastPaintHitPoint) {
    paintLineBetween(*_lastPaintHitPoint, hit.point);
    clearShiftLine();
  } else {
    paintSingleHit(hit);
  }
  _lastPaintHitPoint = hit.point;
  refreshExclusionOverlay();
}

// ── Hover previews ───────────────────────────────────────────────────────────

void ModelSession::setHoverFaces(const std::unordered_set<int32_t>& faces,
                                 uint32_t color) {
  core::Geometry overlay = core::buildExclusionOverlayGeo(
      activeGeo(), setToMask(faces, activeTriCount()));
  _viewer.setHoverPreview(&overlay, color);
}

void ModelSession::clearHover() { _viewer.setHoverPreview(nullptr); }

void ModelSession::updateBrushCursor(double x, double y) {
  if (!_brushIsRadius) {
    _st.brushCursorVisible = false;
    return;
  }
  RaycastHit hit = frontHit(x, y);
  if (!hit.hit) {
    _st.brushCursorVisible = false;
    return;
  }
  // Pixel-accurate circle: project hit point and a point one brush radius
  // along the camera-right axis, take the screen-space distance.
  const double r = brushRadiusActual();
  Vec3 edgePt = hit.point + _viewer.cameraRight() * r;
  double cx, cy, ex, ey;
  _viewer.worldToScreen(hit.point, cx, cy);
  _viewer.worldToScreen(edgePt, ex, ey);
  _st.brushCursorVisible = true;
  _st.brushCursorX = cx;
  _st.brushCursorY = cy;
  _st.brushCursorRadiusPx = std::sqrt((ex - cx) * (ex - cx) +
                                      (ey - cy) * (ey - cy));
}

void ModelSession::updateBrushHover(double x, double y) {
  RaycastHit hit = frontHit(x, y);
  if (!hit.hit) {
    _lastHoverTriIdx = -1;
    clearHover();
    return;
  }
  if (hit.faceIndex == _lastHoverTriIdx) return;
  _lastHoverTriIdx = hit.faceIndex;

  const uint32_t hoverColor = _eraseMode ? 0x999999 : 0xffee00;
  std::unordered_set<int32_t> hovered;
  if (_brushIsRadius) {
    const double r = brushRadiusActual();
    const double r2 = r * r;
    bfsBrushSelect(hit.faceIndex, hit.point, r2, viewDirFor(hit.point),
                   activeGeo(), activeAdj(),
                   [&hovered](int32_t t) { hovered.insert(t); });
  } else {
    hovered.insert(hit.faceIndex);
  }
  setHoverFaces(hovered, hoverColor);
}

void ModelSession::updateBucketHover(double x, double y) {
  int32_t baseIdx = pickBaseFace(x, y);
  if (baseIdx == _lastHoverTriIdx) return; // unchanged — skip expensive BFS
  _lastHoverTriIdx = baseIdx;
  if (baseIdx < 0) {
    clearHover();
    return;
  }
  std::vector<int32_t> filled = core::bucketFill(baseIdx, _adj, _st.bucketAngle);
  if (usingPrecision() && !_precisionParentMap.empty()) {
    std::unordered_set<int32_t> filledSet(filled.begin(), filled.end());
    std::unordered_set<int32_t> refinedHover;
    for (size_t i = 0; i < _precisionParentMap.size(); i++)
      if (filledSet.count(_precisionParentMap[i])) refinedHover.insert((int32_t)i);
    setHoverFaces(refinedHover, _eraseMode ? 0x999999 : 0xffee00);
  } else {
    std::unordered_set<int32_t> hovered(filled.begin(), filled.end());
    setHoverFaces(hovered, _eraseMode ? 0x999999 : 0xffee00);
  }
}

void ModelSession::updatePlaceOnFaceHover(double x, double y) {
  RaycastHit hit = frontHit(x, y);
  if (!hit.hit) {
    _lastHoverTriIdx = -1;
    clearHover();
    return;
  }
  if (hit.faceIndex == _lastHoverTriIdx) return;
  _lastHoverTriIdx = hit.faceIndex;
  std::unordered_set<int32_t> hovered{hit.faceIndex};
  setHoverFaces(hovered, 0xffee00);
}

void ModelSession::clearShiftLine() {
  if (_shiftLineShown) {
    _viewer.setShiftLine(false);
    _shiftLineShown = false;
  }
}

// ── Place on face / rotate ───────────────────────────────────────────────────

void ModelSession::setPlaceOnFace(bool active) {
  _st.placeOnFaceActive = active;
  if (active) {
    // Deactivate conflicting modes
    _st.paintingEnabled = false;
    if (_rotateActive) {
      _st.rotateGizmo = false;
      setRotateMode(false);
    }
    if (_precisionActive) deactivatePrecisionMasking();
  } else {
    _lastHoverTriIdx = -1;
    clearHover();
  }
}

void ModelSession::setRotateMode(bool active) {
  if (_rotateActive == active) return;
  _rotateActive = active;
  if (active) {
    if (_st.placeOnFaceActive) setPlaceOnFace(false);
    _st.paintingEnabled = false;
    // Deviation from main.js (which allows rotating while precision masking
    // stays active): ModelSession's rotate path always operates on the base
    // geometry, so precision must be baked/discarded first to keep the
    // viewer's displayed mesh consistent with what rotateGeometry() edits.
    if (_precisionActive) deactivatePrecisionMasking();
    // Snapshot original positions + pose for the reset button
    _rotateOriginalPositions = _geo.positions;
    _rotatePoseRotSnapshot = _poseRot;
    _rotatePoseTransSnapshot = _poseTrans;
    _rotateAngles[0] = _rotateAngles[1] = _rotateAngles[2] = 0;
    _st.rotX = _st.rotY = _st.rotZ = 0;
    _viewer.setRotationGizmo(true, [this](int axis, double deltaDeg) {
      // handleGizmoDrag()
      _rotateAngles[axis] = std::fmod(_rotateAngles[axis] + deltaDeg, 360.0);
      _st.rotX = std::round(_rotateAngles[0] * 100.0) / 100.0;
      _st.rotY = std::round(_rotateAngles[1] * 100.0) / 100.0;
      _st.rotZ = std::round(_rotateAngles[2] * 100.0) / 100.0;
      static const Vec3 kAxes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
      rotateGeometry(
          Quat::fromAxisAngle(kAxes[axis], render::degToRad(deltaDeg)));
    });
  } else {
    _viewer.setRotationGizmo(false);
    _rotateOriginalPositions.clear();
    rotateFinalize(); // full rebuild now that rotation is done
  }
}

void ModelSession::applyRotation(double xDeg, double yDeg, double zDeg) {
  // applyRotationFromInputs(): targets are relative to rotate-mode entry;
  // the UI resets its fields after Apply, so treat the values as the delta.
  const double dx = xDeg - (_rotateActive ? _rotateAngles[0] : 0);
  const double dy = yDeg - (_rotateActive ? _rotateAngles[1] : 0);
  const double dz = zDeg - (_rotateActive ? _rotateAngles[2] : 0);
  if (std::abs(dx) < 0.001 && std::abs(dy) < 0.001 && std::abs(dz) < 0.001)
    return;
  Quat quat = Quat::fromEulerXYZ(render::degToRad(dx), render::degToRad(dy),
                                 render::degToRad(dz));
  if (_rotateActive) {
    _rotateAngles[0] = xDeg;
    _rotateAngles[1] = yDeg;
    _rotateAngles[2] = zDeg;
    rotateGeometry(quat);
  } else {
    // Apply button outside rotate mode: rotate + finalize in one step
    rotateGeometry(quat);
    rotateFinalize();
  }
}

void ModelSession::resetRotation() {
  if (!_rotateActive || _rotateOriginalPositions.empty()) return;
  _geo.positions = _rotateOriginalPositions;
  _poseRot = _rotatePoseRotSnapshot;
  _poseTrans = _rotatePoseTransSnapshot;
  _geo.normals = computeFaceNormals(_geo); // computeVertexNormals
  _rotateAngles[0] = _rotateAngles[1] = _rotateAngles[2] = 0;
  _st.rotX = _st.rotY = _st.rotZ = 0;
  // Light update only — still in rotate mode
  _faceNormalAttr = computeFaceNormals(_geo);
  _smoothNormalAttr = computeSmoothNormals(_geo);
  pushGeometry(/*fullReload=*/false);
  _viewer.updateRotationGizmo();
}

void ModelSession::rotateGeometry(const Quat& quat) {
  auto& pos = _geo.positions;
  for (size_t i = 0; i + 2 < pos.size(); i += 3) {
    Vec3 v = quat.rotate({pos[i], pos[i + 1], pos[i + 2]});
    pos[i] = (float)v.x;
    pos[i + 1] = (float)v.y;
    pos[i + 2] = (float)v.z;
  }
  _poseRot = _poseRot.premultiplied(quat).normalized();
  _poseTrans = quat.rotate(_poseTrans);
  _geo.normals = computeFaceNormals(_geo); // computeVertexNormals
  // Light update: swap geometry, no camera/grid rebuild (setMeshGeometry)
  _faceNormalAttr = computeFaceNormals(_geo);
  _smoothNormalAttr = computeSmoothNormals(_geo);
  pushGeometry(/*fullReload=*/false);
  _viewer.updateRotationGizmo();
}

void ModelSession::rotateFinalize() {
  if (_geo.positions.empty()) return;
  // Re-center, folding the shift into the pose transform
  core::Bounds bo = core::computeBounds(_geo);
  auto& pos = _geo.positions;
  for (size_t i = 0; i + 2 < pos.size(); i += 3) {
    pos[i] = (float)(pos[i] - bo.center.x);
    pos[i + 1] = (float)(pos[i + 1] - bo.center.y);
    pos[i + 2] = (float)(pos[i + 2] - bo.center.z);
  }
  _poseTrans = _poseTrans - bo.center;

  // Full refresh: bounds, adjacency, attributes, overlay, preview
  _st.settings.cylinderCenterX.reset(); // silhouette bitmap stale; settings
  _st.settings.cylinderCenterY.reset(); // kept null until the cyl panel lands
  _st.settings.cylinderRadius.reset();
  _falloffDirty = true;
  recomputeGeometryDerived();
  pushGeometry(/*fullReload=*/true);
  if (onGeometryChanged) onGeometryChanged();
}

// ── Precision masking ────────────────────────────────────────────────────────

double ModelSession::computePrecisionEdgeLength(double brushDiameter) const {
  // ~20 edge segments around the brush circumference, clamped to a floor.
  return std::max(0.05, render::kPi * brushDiameter / 20.0);
}

int32_t ModelSession::pickBaseFace(double x, double y) const {
  RaycastHit hit = frontHit(x, y);
  if (!hit.hit) return -1;
  if (usingPrecision() && !_precisionParentMap.empty() &&
      (size_t)hit.faceIndex < _precisionParentMap.size())
    return _precisionParentMap[hit.faceIndex];
  return hit.faceIndex;
}

void ModelSession::checkPrecisionOutdated() {
  if (!_precisionActive || _precisionEdgeLength <= 0) {
    _st.precisionOutdated = false;
    return;
  }
  const double neededEdge = computePrecisionEdgeLength(_st.brushRadius);
  // Outdated when the brush shrank enough that the current refinement is
  // too coarse for it.
  _st.precisionOutdated = neededEdge < _precisionEdgeLength * 0.8;
}

void ModelSession::setPrecisionMasking(bool enable) {
  if (enable) {
    if (_st.displacementPreview3D) deactivateDisplacementPreview();
    _precisionActive = true;
    refreshPrecisionMesh();
    if (_precisionGeo.positions.empty()) {
      // Refresh declined/failed (e.g. empty base mesh) — revert.
      _precisionActive = false;
      _st.precisionMasking = false;
    }
  } else {
    deactivatePrecisionMasking();
  }
}

void ModelSession::refreshPrecisionMesh() {
  if (_geo.positions.empty() || _precisionBusy) return;
  _precisionBusy = true;

  const double targetEdge = computePrecisionEdgeLength(_st.brushRadius);
  core::SubdivideResult result =
      core::subdivide(_geo, targetEdge, nullptr, nullptr, /*fast=*/true,
                      core::subdivisionSafetyCap());

  _precisionGeo = std::move(result.geometry);
  _precisionParentMap = std::move(result.faceParentId);
  _precisionEdgeLength = targetEdge;
  _precisionAdj = core::buildAdjacency(_precisionGeo);
  _precisionFaceNormalAttr = computeFaceNormals(_precisionGeo);
  _precisionSmoothNormalAttr = computeSmoothNormals(_precisionGeo);

  _precisionExcludedFaces.clear();
  if (!_excludedFaces.empty())
    for (size_t i = 0; i < _precisionParentMap.size(); i++)
      if (_excludedFaces.count(_precisionParentMap[i]))
        _precisionExcludedFaces.insert((int32_t)i);

  _lastHoverTriIdx = -1;
  clearHover();

  // setMeshGeometry(precisionGeometry) + updateFaceMask(precisionGeometry):
  // the masking tool is still active here, so the falloff gate inside
  // updateFaceMask() skips the expensive recompute (matches main.js).
  updateFaceMask();

  // Force the per-vertex falloff now (bypassing the tool-active gate) so
  // precision mode looks right immediately, matching main.js's explicit
  // computeBoundaryFalloffAttr call right after entering precision mode.
  _maskAttrs = computeMaskAttributes(_precisionGeo, _precisionFaceNormalAttr,
                                     _precisionExcludedFaces, _selectionMode,
                                     maskSettings(), /*computeFalloff=*/true);
  _falloffDirty = false;
  pushGeometry(/*fullReload=*/false);
  if (onMaskChanged) onMaskChanged();

  if (!_precisionExcludedFaces.empty()) refreshExclusionOverlay();

  const size_t triCount = _precisionGeo.positions.size() / 9;
  _st.meshTriangles = triCount;
  char label[48];
  if (triCount >= 1'000'000)
    std::snprintf(label, sizeof(label), "%.1fM triangles", triCount / 1e6);
  else if (triCount >= 1000)
    std::snprintf(label, sizeof(label), "%dk triangles", (int)(triCount / 1000));
  else
    std::snprintf(label, sizeof(label), "%zu triangles", triCount);
  _st.precisionStatusText = label;
  _st.precisionOutdated = false;

  _precisionBusy = false;
}

void ModelSession::deactivatePrecisionMasking() {
  if (!_precisionGeo.positions.empty()) {
    // Bake: the precision geometry becomes the new base geometry.
    _geo = std::move(_precisionGeo);
    _adj = std::move(_precisionAdj);
    _faceNormalAttr = std::move(_precisionFaceNormalAttr);
    _smoothNormalAttr = std::move(_precisionSmoothNormalAttr);
    _excludedFaces = std::move(_precisionExcludedFaces);
    _bounds = core::computeBounds(_geo);
    _st.meshTriangles = triangleCount();
    _st.meshBounds = _bounds;
  } else if (!_precisionExcludedFaces.empty() && !_precisionParentMap.empty()) {
    // No precision geometry but have selections — map back to the base mesh.
    for (int32_t pf : _precisionExcludedFaces)
      if ((size_t)pf < _precisionParentMap.size())
        _excludedFaces.insert(_precisionParentMap[pf]);
  }

  _precisionExcludedFaces.clear();
  _precisionGeo = core::Geometry{};
  _precisionParentMap.clear();
  _precisionEdgeLength = 0;
  _precisionAdj = core::AdjacencyData{};
  _precisionFaceNormalAttr.clear();
  _precisionSmoothNormalAttr.clear();
  _precisionActive = false;
  _st.precisionMasking = false;
  _st.precisionStatusText.clear();
  _st.precisionOutdated = false;
  _lastHoverTriIdx = -1;
  clearHover();

  if (!_geo.positions.empty()) {
    updateFaceMask();
    if (!_excludedFaces.empty()) refreshExclusionOverlay();
  }
}

// ── 3D displacement preview ──────────────────────────────────────────────────
// Port of main.js's toggleDisplacementPreview(). Without this subdivision
// step, enabling vertex displacement on the (typically coarse) base mesh only
// moves its sparse existing vertices — each large flat face just slides along
// its normal as amplitude changes, instead of showing the texture's actual
// bump detail. Subdividing to a moderate preview resolution first gives the
// vertex shader enough geometry to sample the height field meaningfully.

void ModelSession::setDisplacementPreview(bool enable) {
  if (enable) {
    if (_geo.positions.empty() || !_st.hasTexture()) {
      _st.displacementPreview3D = false;
      return;
    }
    _st.paintingEnabled = false; // setExclusionTool(null)
    if (_precisionActive) deactivatePrecisionMasking();
    activateDisplacementPreview();
    if (_dispPreviewGeo.positions.empty()) {
      // Subdivision declined/failed (e.g. empty base mesh) — revert.
      _dispPreviewActive = false;
      _st.displacementPreview3D = false;
    }
  } else {
    deactivateDisplacementPreview();
  }
}

void ModelSession::activateDisplacementPreview() {
  // Target ~maxDim/80 so a 50 mm model gets ~0.6 mm edges — coarser than
  // export for performance, since this rebuilds interactively.
  const double maxDim =
      std::max({_bounds.size.x, _bounds.size.y, _bounds.size.z});
  const double previewEdge = std::max(0.1, maxDim / 80.0);
  const int64_t cap = core::subdivisionSafetyCap();

  core::SubdivideResult sub = core::subdivide(_geo, previewEdge, nullptr,
                                              nullptr, /*fast=*/true, cap);

  core::Geometry activeOut;
  std::vector<int32_t> activeParents;
  if (_st.settings.regularizeEnabled) {
    core::RegularizeResult reg = core::regularizeMesh(
        sub.geometry, sub.faceParentId, previewEdge,
        toRegularizeOpts(_st.settings));

    // Second-pass weights: mark vertices of faces that are (masked-out of
    // displacement anyway via user exclusion) so the re-subdivide doesn't
    // waste resolution on them. Matches main.js exactly — angle masking is
    // deliberately NOT folded in here (unlike the export pipeline's
    // buildCombinedFaceWeights), since this is a coarser preview pass.
    std::vector<float> secondPassStorage;
    std::vector<float>* secondPassWeights = nullptr;
    if (!_excludedFaces.empty() || _selectionMode) {
      const size_t regTriCount = reg.geometry.positions.size() / 9;
      std::vector<uint8_t> mask(regTriCount, 0);
      for (size_t i = 0; i < regTriCount; i++)
        if (_excludedFaces.count(reg.faceParentId[i])) mask[i] = 1;
      secondPassStorage =
          core::buildFaceWeights(reg.geometry, mask, _selectionMode);
      secondPassWeights = &secondPassStorage;
    }

    core::SubdivideResult resub = core::subdivide(
        reg.geometry, previewEdge * _st.settings.regularizeSecondPassMul,
        nullptr, secondPassWeights, /*fast=*/true, cap);

    std::vector<int32_t> composed(resub.faceParentId.size());
    for (size_t i = 0; i < resub.faceParentId.size(); i++)
      composed[i] = reg.faceParentId[resub.faceParentId[i]];

    activeOut = std::move(resub.geometry);
    activeParents = std::move(composed);
  } else {
    activeOut = std::move(sub.geometry);
    activeParents = std::move(sub.faceParentId);
  }

  _dispPreviewGeo = std::move(activeOut);
  _dispPreviewParentMap = std::move(activeParents);
  if (_dispPreviewGeo.positions.empty()) return;

  _dispPreviewFaceNormalAttr = computeFaceNormals(_dispPreviewGeo);
  _dispPreviewSmoothNormalAttr = computeSmoothNormals(_dispPreviewGeo);

  _dispPreviewExcludedFaces.clear();
  if (!_excludedFaces.empty())
    for (size_t i = 0; i < _dispPreviewParentMap.size(); i++)
      if (_excludedFaces.count(_dispPreviewParentMap[i]))
        _dispPreviewExcludedFaces.insert((int32_t)i);

  _lastHoverTriIdx = -1;
  clearHover();

  _dispPreviewActive = true;
  _st.meshTriangles = _dispPreviewGeo.positions.size() / 9;

  // Force the full mask/falloff compute now (bypassing updateFaceMask()'s
  // tool-active gate) so the preview looks right immediately, mirroring
  // refreshPrecisionMesh()'s explicit compute right after entering precision
  // mode.
  _maskAttrs = computeMaskAttributes(_dispPreviewGeo, _dispPreviewFaceNormalAttr,
                                     _dispPreviewExcludedFaces, _selectionMode,
                                     maskSettings(), /*computeFalloff=*/true);
  _falloffDirty = false;
  _st.excludedFaceCount = (int)_dispPreviewExcludedFaces.size();
  pushGeometry(/*fullReload=*/false);
  if (onMaskChanged) onMaskChanged();
}

void ModelSession::deactivateDisplacementPreview() {
  _st.displacementPreview3D = false;
  if (!_dispPreviewActive) return;

  _dispPreviewActive = false;
  _dispPreviewGeo = core::Geometry{};
  _dispPreviewParentMap.clear();
  _dispPreviewFaceNormalAttr.clear();
  _dispPreviewSmoothNormalAttr.clear();
  _dispPreviewExcludedFaces.clear();
  _lastHoverTriIdx = -1;
  clearHover();

  if (!_geo.positions.empty()) {
    _st.meshTriangles = activeTriCount(); // base or precision, whichever resumes
    updateFaceMask(); // reverts to base/precision geo
  }
}

} // namespace app
