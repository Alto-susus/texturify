// Application settings + session state. `Settings` mirrors the `settings`
// object in reference/js/main.js field-for-field (same names, same defaults)
// so .texturify project files and the pipeline behave identically.
#pragma once

#include <optional>
#include <string>

#include "core/geometry.h"

namespace app {

struct Settings {
  int mappingMode = 5; // Triplanar default
  double scaleU = 0.5;
  double scaleV = 0.5;
  double amplitude = 0.5;
  double textureHeight = 0.5;
  bool invertDisplacement = false;
  double offsetU = 0.0;
  double offsetV = 0.0;
  double rotation = 0;
  double refineLength = 1.0;
  int maxTriangles = 750'000;
  bool lockScale = true;
  double bottomAngleLimit = 5;
  double topAngleLimit = 0;
  double mappingBlend = 1;
  double seamBandWidth = 0.5;
  double textureSmoothing = 0;
  int blendNormalSmoothing = 32;
  double capAngle = 20;
  double boundaryFalloff = 0;
  bool symmetricDisplacement = false;
  bool noDownwardZ = false;
  bool smoothBottom = true;
  bool harvestFlatFaces = true;
  double harvestTol = 0.005;
  bool useDisplacement = false;
  // Cylindrical-mode controls; nullopt → derive from bounds.
  bool snapSeamlessWrap = true;
  std::optional<double> cylinderCenterX, cylinderCenterY, cylinderRadius;
  bool cylinderPanelMinimized = false;
  // Regularize Mesh (Advanced/Beta)
  bool regularizeEnabled = true;
  double regularizeAspectThreshold = 5;
  double regularizeSlack = 3.0;
  double regularizeAggressiveSlack = 8.0;
  double regularizeExtremeAspect = 8;
  double regularizeNormalDeg = 15;
  double regularizeAggressiveNormalDeg = 25;
  double regularizeSecondPassMul = 1.1;
};

// Exclusion painting modes (main.js brush state) — wired in Phase 6.
enum class BrushTool { Single, Radius, Bucket };
enum class BrushMode { Exclude, Include };

// Mesh diagnostics highlight kinds (main.js's toggleDiagHighlight `kind`
// argument) — which finding's overlay is currently shown in the viewport.
enum class DiagHighlight { None, OpenEdges, NonManifold, Shells, Intersects, Overlaps };

struct AppState {
  Settings settings;

  // Texture selection: preset index, or -1 with customTextureName set.
  int selectedPreset = -1;
  std::string customTextureName;
  bool hasTexture() const { return selectedPreset >= 0 || !customTextureName.empty(); }

  // Loaded model
  std::string meshName = "cube";
  size_t meshTriangles = 0;
  core::Bounds meshBounds;
  bool meshDirty = false; // baked-vs-live status pill

  // Viewer/view state
  bool wireframe = false;
  bool perspectiveProjection = false;
  bool previewEnabled = true; // live bump preview (viewer material)
  bool displacementPreview3D = false; // GPU displacement preview toggle

  // Left-rail tab: 0 = Textures, 1 = Import
  int leftTab = 0;

  // Exclusion painting UI state (interactions land in Phase 6)
  bool paintingEnabled = false;
  BrushTool brushTool = BrushTool::Single;
  BrushMode brushMode = BrushMode::Exclude;
  // excl-brush-radius-slider value ("Size" label) — a DIAMETER, default 10.
  // main.js: `brushRadius = sliderValue / 2` before use as an actual radius
  // (bfsBrushSelect, cursor sizing); `computePrecisionEdgeLength` takes the
  // raw slider value. ModelSession::brushRadiusActual() applies that /2.
  double brushRadius = 10.0;
  double bucketAngle = 20.0; // excl-threshold default
  int excludedFaceCount = 0;
  bool precisionMasking = false;
  bool precisionOutdated = false;   // brush shrank since last refine
  std::string precisionStatusText;  // e.g. "12k triangles"

  // Rotate controls (Import tab)
  double rotX = 0, rotY = 0, rotZ = 0;
  bool rotateGizmo = false;
  bool placeOnFaceActive = false;

  // Radius-brush cursor circle (viewport-relative viewer pixels; drawn by the
  // HUD — replaces the DOM #excl-brush-cursor element).
  bool brushCursorVisible = false;
  double brushCursorX = 0, brushCursorY = 0, brushCursorRadiusPx = 0;

  // Export/bake progress (app::PipelineRunner, std::thread-backed)
  bool pipelineRunning = false;
  float pipelineProgress = 0;
  std::string pipelineStage;
  std::string pipelineErrorMessage; // non-empty → UI shows a dismissible banner
  bool triLimitWarning = false;     // last export hit the subdivision safety cap
  // "Mask just-baked faces from further texturing" (bake-mask-row checkbox).
  bool bakeKeepMask = true;

  // Mesh diagnostics popup (mirrors main.js's #mesh-diagnostics floating
  // card). Fast checks (open/non-manifold edges, shell count) rerun
  // automatically on every full mesh reload (ModelSession::pushGeometry's
  // fullReload path); advanced checks (intersecting/overlapping triangles)
  // are expensive and run on demand on a background thread
  // (app::DiagnosticsRunner), cancelled if the mesh changes mid-run.
  bool diagHasFast = false;
  int64_t diagOpenEdges = 0, diagNonManifoldEdges = 0, diagShellCount = 0;
  bool diagAdvancedRunning = false;
  bool diagHasAdvanced = false;
  int64_t diagIntersectingPairs = 0, diagOverlappingPairs = 0;
  // User clicked the popup's dismiss (x); reset to false on the next full
  // reload, matching main.js's `meshDiagnostics.classList.remove('hidden')`
  // inside updateMeshDiagnostics().
  bool diagDismissed = false;
  DiagHighlight diagActiveHighlight = DiagHighlight::None;

  // Welcome / License / Imprint modal overlays (main.js's #welcome-overlay /
  // #license-overlay / #imprint-overlay). License/imprint have no persisted
  // state — they're always shown on demand only, closed by the UI directly.
  bool welcomeOpen = false;
  // Mirrors main.js's openWelcome({allowDismissPersist}): true only for the
  // once-per-release auto-popup at startup, so checking "don't show again"
  // there persists the dismiss stamp; a manually-opened "What's New" always
  // passes false, so it can never accidentally suppress a future release's
  // popup.
  bool welcomeAllowDismissPersist = false;
  bool welcomeDontShowAgain = false; // checkbox state while the modal is open
  bool licenseOpen = false;
  bool imprintOpen = false;
};

} // namespace app
