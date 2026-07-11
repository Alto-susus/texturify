# Texturify — porting status

Texturify is inspired by BumpMesh (CNCKitchen/stlTexturizer) — an independent native rewrite, not
the original project. See the License/legal notes further down for the required AGPL-3.0
attribution to the original work.

Native C++ port of `reference/` (CNCKitchen/stlTexturizer, AGPL-3.0). Plan:
`C:\Users\Sonicmogus\.claude\plans\c-users-sonicmogus-downloads-displaceme-validated-kettle.md`

## Build

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release          # targets: texturify, texturify_verify
```

macOS / Linux (see "Cross-platform port" below for what's platform-specific):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -- -j
```

## Cross-platform port

Originally Windows-only (unconditional `windows.h` in three app-layer files, Windows-only CMake
link flags). Ported to also build real, functional macOS and Linux binaries — this was a genuine
native port (file dialogs, app-data paths, asset resolution, CMake/link config), not just a
packaging exercise, done in a session with no macOS/Linux machine available locally, so correctness
was verified via `texturify_verify`'s golden suite locally (Windows) plus real compilation on
GitHub Actions' `macos-latest`/`ubuntu-latest` runners (see `.github/workflows/release.yml`) rather
than local interactive testing on those OSes.

- **`src/app/app_paths.{h,cpp}`** (new): the one cross-platform per-user-data-dir helper, replacing
  what used to be two copy-pasted Windows-only `%APPDATA%` blocks in `native_prefs.cpp` and
  `session_autosave.cpp`. Windows: `%APPDATA%\Texturify`; macOS: `~/Library/Application
  Support/Texturify`; Linux: `$XDG_CONFIG_HOME/Texturify` or `~/.config/Texturify`. Uses
  `std::filesystem::create_directories` (portable) instead of `CreateDirectoryA`.
- **Native file dialogs**, one TU per platform, selected in `CMakeLists.txt` (all three define the
  same `showOpenFileDialog`/`showSaveFileDialog` from `file_dialog.h`, so exactly one may be
  compiled in):
  - `file_dialog_win32.cpp` — unchanged, existing `GetOpenFileNameA`/`GetSaveFileNameA`.
  - `file_dialog_linux.cpp` (new) — no single toolkit ships on every Linux desktop, so this shells
    out via `popen` to `zenity` (GNOME/Ubuntu; the `.deb`'s `Recommends:`) or `kdialog` (KDE)
    fallback, translating the `;`-separated extension filters (`"*.stl;*.obj;*.3mf"`) into each
    tool's own syntax. Returns `nullopt` if neither is installed. Save dialogs append the default
    extension if the user's typed filename has none (neither zenity nor kdialog do this
    automatically the way Windows' `lpstrDefExt` does).
  - `file_dialog_macos.mm` (new, Objective-C++) — `NSOpenPanel`/`NSSavePanel`, using the (deprecated
    but still functional on all current macOS SDKs) `allowedFileTypes` rather than pulling in the
    `UniformTypeIdentifiers` framework for `allowedContentTypes`.
- **`src/app/mac_bundle_path.{h,mm}`** (new): a tiny Objective-C++ TU exposing
  `macBundleResourcePath()` (wraps `NSBundle.mainBundle.resourcePath`) so `main.cpp` itself can stay
  a plain `.cpp` file instead of becoming `.mm` just for this one call.
- **`resolveAssetDir()` (`main.cpp`)** gained real Linux (`readlink("/proc/self/exe")`) and macOS
  (`macBundleResourcePath()` + `Contents/Resources/assets`, since the .app bundles assets there —
  see the CMake post-build copy step below) branches alongside the existing Windows
  `GetModuleFileNameA` one, using `std::filesystem` for the join/is-directory checks on both new
  branches. Linux additionally falls back to `/usr/share/texturify/assets` (the `.deb`'s installed
  location — `/usr/bin/texturify` has no `assets/` folder beside it there, unlike a local build).
- **`CMakeLists.txt`**: platform-conditional executable definition (`WIN32` flag + `app.rc` on
  Windows only; `MACOSX_BUNDLE` + `app.icns` + `MACOSX_BUNDLE_*` properties on Apple; plain
  executable on Linux), conditional link libraries (`opengl32 comdlg32 ole32 shell32` on Windows;
  `-framework Cocoa` on macOS for the two new `.mm` files' own Foundation/AppKit calls — GLFW's own
  frameworks/X11/Threads/dl links are already private to the `glfw` static-lib target and propagate
  to the final executable's link line automatically, standard CMake static-library behavior, so
  nothing extra was needed there), and `file(GLOB APP_SOURCES ...)` now explicitly removes both
  `file_dialog_win32.cpp` and `file_dialog_linux.cpp` before re-adding exactly one per platform (the
  `.mm` files are naturally excluded from a `*.cpp` glob already). Also disabled
  `GLFW_BUILD_WAYLAND` (defaults ON upstream on Linux, needs `wayland-scanner`/`wayland-protocols`
  at configure time) — X11 alone is sufficient and keeps the Linux dependency list to `xorg-dev
  libgl1-mesa-dev`. Per-platform asset-copy post-build steps: next to the `.exe` (Windows, unchanged),
  into `Contents/Resources/assets` inside the bundle (macOS, new), next to the binary for local/dev
  builds on Linux (new) — the `.deb` separately `install()`s assets to `/usr/share/texturify/assets`
  (FHS-correct location; `/usr/bin` shouldn't have a data folder beside it) plus a `.desktop` entry
  and a 256px PNG icon, packaged via CPack's `DEB` generator (`CPACK_DEBIAN_PACKAGE_DEPENDS` for the
  X11/GL runtime libs GLFW needs; `CPACK_DEBIAN_PACKAGE_RECOMMENDS "zenity"` rather than `Depends`,
  since `kdialog` is an equally valid fallback and the app degrades gracefully — file dialogs just
  return "cancelled" — if neither is present).
- **`assets/icon/app.icns`** (new) and **`assets/icon/icon_256.png`** (new, Linux `.desktop` icon):
  generated from the same `source.png` as the existing Windows `.ico`/PNGs, via Pillow's pure-Python
  ICNS encoder (`Image.save(..., sizes=[...])`) — no `iconutil`/macOS needed to generate it.
  `packaging/linux/texturify.desktop` (new) is the `.deb`'s applications-menu entry.
- **`.github/workflows/release.yml`** (new): on any `v*` tag push (or manual
  `workflow_dispatch`, which builds and uploads artifacts but skips the final release-creation job —
  see its `if:` condition), builds on `windows-latest`/`macos-latest`/`ubuntu-latest` in parallel,
  runs `texturify_verify`'s full golden suite on all three (meaningful in CI because
  `tools/golden/out/`'s ~10MB of pre-dumped fixtures are committed to the repo — unlike `reference/`,
  that directory was never `.gitignore`d — so no Node.js/reference-JS install is needed in CI at
  all), packages each (`.zip` via `Compress-Archive` on Windows, `.dmg` via `hdiutil` on macOS,
  `.deb` via CPack on Linux), then a fourth job downloads all three artifacts and publishes them to
  a GitHub Release via `softprops/action-gh-release`. Gated on `contents: write` permissions and a
  real tag push, not just any commit.

## Fidelity contract (do not break)

- Buffers are `float` (JS Float32Array); intermediate math in `double` (JS numbers); store back to float.
- Weld grids per module: 1e4 (export/repair/validation/exclusion/masking), 1e5 (subdivision/regularize/displacement), 1e6 (decimation). See `reference/CONTEXT.md`.
- Pipeline order: subdivide → [regularize → re-subdivide] → displace → [decimate, export only] → bottom clamp → smooth bottom → [resolveTJunctions when decimated].
- Port QuantizedPointMap's open-addressing behavior exactly — iteration order matters downstream.

## Porting map (JS → C++), with status

| reference/js | src | status |
|---|---|---|
| meshIndex.js | core/mesh_index.{h,cpp} | done, golden-verified (bit-identical) |
| stlLoader.js | core/loaders.{h,cpp} | done, round-trip tested |
| exporter.js | core/exporter.{h,cpp} | done, round-trip tested |
| mapping.js | core/mapping.{h,cpp} | done, golden-verified (bit-identical) |
| displacement.js | core/displacement.{h,cpp} | done, golden-verified (bit-identical; trig paths ≤1e-5) |
| subdivision.js | core/subdivision.{h,cpp} | done, golden-verified (bit-identical) |
| regularize.js | core/regularize.{h,cpp} | done, golden-verified (bit-identical) |
| decimation.js | core/decimation.{h,cpp} | done, golden-verified (bit-identical) |
| meshRepair.js | core/mesh_repair.{h,cpp} | done, golden-verified (bit-identical) |
| meshValidation.js | core/mesh_validation.{h,cpp} | done, golden-verified |
| exclusion.js | core/exclusion.{h,cpp} | done, golden-verified (bit-identical) |
| smartResolution.js | core/smart_resolution.{h,cpp} | done, golden-verified |
| textureAnalysis.js | core/texture_analysis.{h,cpp} | done, golden-verified |
| exportPipeline.js | core/export_pipeline.{h,cpp} | done, golden-verified (export+bake bit-identical) |
| main.js blur helpers | core/image.{h,cpp} | done (box blur golden-verified bit-identical; resize ≈ canvas bilinear) |
| viewer.js | render/viewer.{h,cpp} | done (cameras/orbit/zoom/grid/axes/dims/wireframe/overlays/gizmo; three r170 ACES 1.1 + sRGB + physical light units replicated in GLSL; GridHelper toneMapped:false honored; preview ShaderMaterial NOT tone-mapped) |
| previewMaterial.js | render/preview_material.{h,cpp} | done (shared GLSL near-verbatim) |
| presetTextures.js | app/preset_textures.{h,cpp} | done (24 presets, 512 cap, cache; thumbs generated from full images — stb can't decode the .webp thumbs) |
| i18n.js + i18n/*.js | app/i18n.{h,cpp} + assets/lang/*.json | backend done (loader/t()/lang switch, see below); UI retrofit partial — panels_right.cpp, panels_left.cpp, diag_panel.cpp, cylinder_panel.cpp, modals.cpp, and the toolbar now call t() wherever a matching original key exists; native-only chrome with no web-app equivalent (New/Open/Save/Undo/Redo, Regularize-advanced sub-sliders, viewport-mode chips, section headings invented for this port) stays hardcoded English — see below for the exact list |
| main.js settings/state | app/app_state.h | done (exact defaults; brushRadius 10, bucketAngle 20) |
| main.js addFaceNormals/addSmoothNormals + updateFaceMask defaults | app/preview_attributes.{h,cpp} | done (weld 1e4; the shader reads 0 for missing attrs → whole model renders masked orange without these) |
| main.js updateFaceMask/computeBoundaryFalloffAttr/computeBoundaryEdges/bfsBrushSelect | app/masking.{h,cpp} | done (falloff grid, 64-edge cap, PrusaSlicer-style BFS brush) |
| main.js rotate mode, place-on-face, exclusion painting, precision masking | app/actions.{h,cpp} (ModelSession) | done |
| main.js cylinder axis panel (_buildCylinderSilhouette/autoFitCylinderAxis) | app/cylinder_silhouette.{h,cpp} + ui/cylinder_panel.cpp | done |
| main.js UI wiring (5781 ln) | src/ui/* + main.cpp | Phase 5 shell + Phase 6 interactions done; Phase 7 partial (see below) |
| exportWorker.js / handleExport / bakeTextures | app/pipeline_runner.{h,cpp} (std::thread) | done |
| native file dialogs (`<input type=file>`/download-anchor) | app/file_dialog.{h,cpp} | done (GetOpenFileNameA/GetSaveFileNameA) |
| applySmartResolution / checkAmplitudeWarning / checkResolutionWarning | ui/panels_right.cpp | done |
| undo/redo | app/undo_stack.{h,cpp} | done |
| .texturify project save/load | app/project_file.{h,cpp} | done (simplified — see below) |
| sessionStorage autosave | app/session_autosave.{h,cpp} | done (persists across restarts, unlike sessionStorage) |
| JSON (de)serialization | app/json.{h,cpp} | done (hand-rolled, no vendored library) |
| mesh diagnostics popup (fast + advanced checks, Show/Hide overlays) | app/diagnostics_runner.{h,cpp} + ui/diag_panel.cpp | done |
| welcome/license/imprint overlays | ui/modals.cpp + app/native_prefs.{h,cpp} | done (see below) |
| full i18n string retrofit (data-i18n sweep across the whole UI) | — | mostly done (see i18n section below); remaining gaps are all native-only chrome with no original key |

## Verification

- `tools/golden/*.mjs` (Node 24) dump reference-JS outputs for fixtures → `tools/golden/out/`.
  Requires `npm install` deps inside `reference/` (three@0.170.0, fflate — git-ignored).
- `texturify_verify` runs C++ port on same fixtures, diffs (bitwise topology, ≤1e-5 float coords).
  **Must be run from the repo root**, not `build/Release` — it resolves `tools/golden/out/` as a
  path relative to the process's current working directory (`tools/verify/verify_pipeline.cpp`'s
  `kOut`), so running it from `build/Release` silently SKIPs every golden-comparison block instead
  of failing loudly (easy to mistake for "golden dumps not generated yet" when they actually are).
- Status: ALL green — weld/loaders/exporters + exclusion, mapping (all 7 modes × 3 variants),
  texture analysis, smart resolution, subdivision (accurate+fast), regularize, displacement
  (triplanar bit-identical, cylindrical ≤1e-5), decimation, T-junction repair, snapBottomToFlat,
  mesh validation, box blur, and the full export & bake pipelines — bit-identical vs reference JS.
- Hard-won fidelity details: `js::round` must return -0.0 for x∈[-0.5,0) (meshRepair writes
  round(x*Q)/Q into output buffers); never cache `.data()` pointers across VertStore growth.

## Phase 8: end-to-end verification vs the reference web app

Ran the full golden-dump suite (previously silently SKIPping — see above) plus a live comparison
against `reference/` served locally (`python -m http.server` + the Browser tool; equivalent to
bumpmesh.com's deployed build, same source). This surfaced and fixed two real divergences the
synthetic Node fixtures never exercised (both were in app-layer code, not core/ — the golden suite
only covers core/):

- **Default cube was the wrong size**: `main.cpp`'s `makeDefaultCube()` built a 20×20×20 mm cube;
  main.js's `loadDefaultCube()` builds 50×50×50 mm (`new THREE.BoxGeometry(50, 50, 50)`). Fixed
  (`h = 25.0f`) and renamed the default mesh name to `"cube_50x50x50"` to match `currentStlName`.
- **`refineLength` (export resolution) never got its per-model default**: main.js recomputes
  `refineLength = clamp(0.05, 5.0, round(diag/250, 2))` from the bounding-box diagonal in BOTH
  `loadDefaultCube()` AND `handleModelFile()` — i.e. on every fresh geometry load. This port only
  had that calculation in `ctx.actions.resetSettings` ("Reset to Defaults"); `ModelSession::
  setGeometry()` never touched it, so `refineLength` stayed stuck at the compiled-in `1.0` default
  for the default cube AND every "Load model" call — silently coarser or finer than intended
  depending on model size, and the reason a live Crystal-preset export from a 50 mm cube came out
  70,036 triangles here vs. 363,104 in the reference (1.0 mm vs. the correct 0.35 mm target edge).
  Fixed by moving the diag/250 computation into `setGeometry()` itself (`actions.cpp`), so it applies
  uniformly to the startup cube, "Load model", and `--test-load`. Verified safe for `.texturify`
  project import (`main.cpp`'s `doOpenProjectFrom`): `setGeometry()` still runs before
  `applySettingsSnapshot()`, so a project's saved `refineLength` correctly overrides this default
  afterward, matching main.js's `handleModelFile()` → `applySettingsSnapshot(data)` ordering.
  Re-exporting the same Crystal/50mm/refineLength-0.35 configuration after the fix: 366,950 triangles
  vs. reference's 363,104 (~1%, bounds match to 4 decimal places) — the residual gap is consistent
  with stb_image vs. browser canvas texture-decode differences, not a logic bug (the golden suite
  already proves the core algorithms bit-identical given identical input pixels).
- **`.texturify` project interchange, verified for real**: exported a project from this port
  (`--test-project`) with a non-default UV rotation (12°) and texture height (0.33mm), then imported
  it into the live reference app via a synthesized `File`/`DataTransfer` dispatched at
  `#import-project-input`'s `change` handler (real app code path, not a mock) — the reference app's
  load-mode dialog (`#load-mode-all`/`#load-go-btn`) correctly offered replace-model-and-settings,
  and after confirming, model geometry (12 tris, 50×50×50mm), active texture ("Crystal"), texture
  height (0.33), refineLength (0.35), and UV rotation (12°) all came through correctly. Confirms the
  ZIP structure, `settings.json` schema, and `model.stl` pose convention are real, working
  interoperability with the original app — not just this port's own round-trip.
- UI layout/style (glass panels, accent color, toolbar, viewport HUD) was already screenshot-verified
  against the "1a Docked Studio" mockup section in earlier sessions (see UI section below); the
  mockup itself is a static, non-interactive multi-variant design gallery (unrendered `{{ }}`
  template placeholders, generic "Spectre"-branded placeholder content) — it was always a style/
  layout reference, not literal copy to replicate, so section labels/content intentionally follow
  Texturify's actual feature set rather than the mockup's generic "MATERIAL"/"BRUSH"/"SCENE MESHES"
  placeholders.

## UI

New "Spectre Displace 1a" glass UI (mockup: `C:\Users\Sonicmogus\Downloads\Displacement Studio.dc.html`).
Accent #ff2d55, bg #0a0a0e, glass rails left (textures) / right (params), toolbar, viewport HUD.
Fonts: Instrument Sans (400/500/600/700 static), JetBrains Mono, Noto Sans (+JP/KR on demand).

Phase 5 shell is live: ui/{theme,glass,widgets,ui_shell,toolbar,panels_left,panels_right,viewport_ui}
+ rewired main.cpp. Notes:
- Gradient fills must use `ui::roundedGradientV` (ImGui AddRectFilledMultiColor is square-only).
- App is WIN32 subsystem — WinMain forwards `__argc/__argv`. Debug flags: `--screenshot <png>`
  (dumps frame 6, exits), `--preset <idx>` (pre-selects a texture), `--mapping <idx>` (pre-selects a
  mapping mode, e.g. 3 = Cylindrical to screenshot the cylinder-axis panel), `--test-export
  <path.stl|path.3mf>` / `--test-bake` (bypasses the save dialog and runs a real export/bake through
  `PipelineRunner`, polls to completion, prints `export: OK (N tris)` or the error to stdout/stderr,
  exit code reflects success — the same worker-thread code path the buttons use, requires `--preset`),
  `--test-undo` (exercises undo/redo + JSON + session-autosave round-trips, no window needed),
  `--test-project <path.texturify>` (save+reload round-trip through the same code the toolbar
  Save/Open buttons use, bypassing both native dialogs), `--test-load <path.stl|obj|3mf>` (loads a
  model at startup through the same `core::loadModelFile` + `session.setGeometry` path as the
  toolbar's Load button, bypassing the native file-open dialog — e.g. to screenshot the mesh
  diagnostics popup against a mesh with known defects), `--lang <code>` (forces the UI language at
  startup, overriding the saved preference — also exercises the CJK/Cyrillic font-atlas-rebuild path,
  see i18n below), `--test-modal <welcome|license|imprint>` (forces that modal open at startup,
  bypassing the real trigger — auto-popup logic and toolbar buttons — for screenshot verification),
  `--test-disp-preview` (forces the 3D displacement-preview toggle on at startup through the same
  `ModelSession::setDisplacementPreview` path as the sidebar checkbox, requires `--preset`),
  `--wireframe` (forces wireframe view at startup), `--amplitude <mm>` (overrides texture height at
  startup) — the latter three exist for screenshotting the displacement-preview subdivision fix below
  for UI/pipeline verification.
- Viewer renders into GlassCompositor's MSAA viewport target; pointer events route to the viewer
  only when the cursor is inside the viewport rect and !io.WantCaptureMouse.

Phase 6 interactions are live via `app::ModelSession` (src/app/actions.{h,cpp}), constructed once
in main.cpp around AppState + Viewer:
- Rotate mode: gizmo drag (`viewer.setRotationGizmo` callback) and the Import-tab X/Y/Z inputs both
  fold into `currentPoseRot`/`currentPoseTrans` (undone on export, not yet wired — Phase 7); Reset
  restores the rotate-mode-entry snapshot; Apply/gizmo-release finalizes (re-center + full rebuild).
- Place on face: click raycasts (`Viewer::raycastFront`, the getFrontFaceHit port), rotates the
  picked face normal to -Z via `Quat::fromUnitVectors`, re-centers, forces a full geometry reload.
- Exclusion painting: Single/Radius(BFS)/Bucket tools, Exclude/Include mode, Shift = erase (checked
  live each frame, not latched), Ctrl+click = line-fill between the last paint point and the click
  (`_paintLineBetween`'s screen-space resample, via new `Viewer::worldToScreen`/`setShiftLine`).
  Hover previews and the radius-brush cursor circle are HUD-drawn (`viewport_ui.cpp`), not DOM
  overlays. `UiActions::maskSettingsChanged` (bottom/top angle limit, boundary falloff sliders only)
  is distinct from `settingsChanged` — it triggers the costlier CPU boundary-falloff/edge recompute
  (`ModelSession::updateFaceMask`), matching main.js's `_falloffDirty` gate; other settings (scale,
  amplitude, ...) stay live via shader uniforms alone.
- Pointer dispatch in main.cpp: gizmo-grab always wins (bundled inside `Viewer::onPointerDown`);
  `ModelSession::onPointerDown` gets first refusal only while paintingEnabled or placeOnFaceActive
  is set, and a `false` return (miss, or bucket which never blocks orbit) falls through to the
  viewer for normal orbit/pan — mirrors main.js's independent canvas-mousedown vs. OrbitControls
  listeners without literally having two listeners.
- Precision masking (adaptive local remesh under the radius brush, for finer exclusion-painting
  granularity than the base topology allows) is live: `ModelSession` gains an `activeGeo()`/
  `activeAdj()`/`activeFaceNormalAttr()`/`activeSmoothNormalAttr()`/`activeExcludedFaces()` accessor
  pair selecting base vs. precision state, used throughout painting/hover/mask code so one code path
  serves both. `setPrecisionMasking(true)` calls `refreshPrecisionMesh()`, which locally
  `core::subdivide(..., fast=true)`s the base mesh at `computePrecisionEdgeLength(brushDiameter)` =
  `max(0.05, π·brushDiameter/20)` — note this takes the RAW (unhalved) brush-radius-slider value,
  while every geometric use site (BFS distance-squared, cursor sizing) uses
  `ModelSession::brushRadiusActual()` = `st.brushRadius * 0.5`, because the "Brush radius" UI slider
  (range 0.2–100, default 10, labeled "Size") is actually a **diameter** in main.js
  (`brushRadius = sliderValue / 2` before geometric use). `deactivatePrecisionMasking()` bakes the
  precision geometry into the new base mesh (or projects precision-space selections back via the
  parent map if no refinement exists yet). Bucket fill is an exception to the base/precision split:
  it always walks BASE adjacency/face-indices (`pickBaseFace()`), fills base `excludedFaces`, then
  projects into `precisionExcludedFaces` via the parent map — brush painting instead operates
  directly in whichever geometry (base or precision) is currently active. `checkPrecisionOutdated()`
  runs every frame (`ModelSession::update`) and flags `st.precisionOutdated` when the brush has
  shrunk enough that the current refinement is too coarse; the right-rail masking section shows a
  "Brush size changed" + Refresh button (`ctx.actions.refreshPrecisionMesh`) when that's set.
  Deliberate architectural deviation: main.js allows rotating the mesh while precision masking stays
  active; this port forces precision off before entering rotate mode (`setRotateMode(true)` calls
  `deactivatePrecisionMasking()`) because `rotateGeometry()`/`rotateFinalize()` operate on `_geo`
  directly, not `activeGeo()` — same for place-on-face.
- **Bug fixed** (found via user report "texture height changing just scaling planes"): the "3D
  Preview" toggle (`st.displacementPreview3D`) only ever flipped the shader's `useDisplacement`
  uniform — it never subdivided the mesh first, so vertex displacement ran on whatever coarse
  topology `activeGeo()` already held (often a handful of triangles per face). With so few vertices
  to move, increasing texture height just slid each large flat face along its normal — visually
  indistinguishable from scaling a plane, not real bump geometry. Fixed by porting main.js's
  `toggleDisplacementPreview()` (main.js:4257-4390), which was never ported at all: `ModelSession`
  gains `activateDisplacementPreview()`/`deactivateDisplacementPreview()` plus `_dispPreviewActive`/
  `_dispPreviewGeo`/`_dispPreviewParentMap`/`_dispPreviewFaceNormalAttr`/
  `_dispPreviewSmoothNormalAttr`/`_dispPreviewExcludedFaces` state, and `activeGeo()` and friends now
  check `_dispPreviewActive` first (ahead of the precision-masking check — the two are mutually
  exclusive, each deactivates the other on entry, exactly like main.js's
  `if (precisionMaskingEnabled) deactivatePrecisionMasking()`). Activation subdivides the base mesh
  to `previewEdge = max(0.1, maxDim/80)` (coarser than export, since this reruns interactively),
  then — when `settings.regularizeEnabled` — regularizes and re-subdivides exactly like the export
  pipeline (`core/export_pipeline.cpp`'s `subdivide → regularize → re-subdivide`), including the
  second-pass exclusion weights main.js derives from `excludedFaces`/`selectionMode` (deliberately
  *not* folding in angle masking here, unlike `buildCombinedFaceWeights` — matches main.js, which
  only optimizes the preview's second pass by user selection, not by angle). `setDisplacementPreview`
  requires `st.hasTexture()` (mirrors `!activeMapEntry` in main.js) and reverts
  `st.displacementPreview3D` to false if there's no model/texture yet or subdivision yields empty
  geometry. The three call sites that used to just flip the flag directly (masking-tool activation in
  `update()`, place-on-face in `onPointerDown()`, precision-masking activation in
  `setPrecisionMasking()`) now call `deactivateDisplacementPreview()` instead, so the real subdivided
  mesh gets torn down (not just the UI flag) whenever one of those mutually-exclusive modes takes
  over. `toRegularizeOpts()` (previously `pipeline_runner.cpp`-local, in an anonymous namespace) was
  hoisted to file scope and declared in `pipeline_runner.h` so `actions.cpp` could reuse it rather
  than duplicating the Settings → RegularizeOpts mapping. Verified via `--test-disp-preview`
  screenshots at `--amplitude 2.0`: with the toggle off, a 20 mm cube stays a perfect flat cube at
  2 mm texture height (bump-only shading, as designed); with it on, the same settings produce a
  visibly jagged, genuinely displaced 196k-triangle mesh — confirming the fix. `st.meshTriangles`
  (the viewport's verts/tris HUD readout) is now also updated on activation/deactivation, which
  main.js has no direct equivalent for (no separate preview-mode triangle counter in the original UI)
  but was necessary here to make the state change externally observable/debuggable.
  **Follow-up regression fixed same day** (user report "model loading shows orange cube"):
  `ModelSession::setGeometry()` resets precision-masking state on a fresh model load but was never
  updated to also reset the new `_dispPreviewActive`/`_dispPreviewGeo`/`_dispPreviewParentMap`/
  `_dispPreviewFaceNormalAttr`/`_dispPreviewSmoothNormalAttr`/`_dispPreviewExcludedFaces` fields.
  Repro: enable 3D Preview, then load a different model — `activeGeo()` kept returning the stale
  (differently-sized) preview mesh from the OLD model while `recomputeGeometryDerived()` sized the
  mask/normal attribute buffers for the NEW model's triangle count; the resulting buffer-size mismatch
  reads as all-zero mask attributes in the shader, which renders as solid `userMaskColor` (orange)
  across the whole model. Fixed by adding the same reset block (mirroring the precision-masking one
  right above it) to `setGeometry()`, plus resetting `st.displacementPreview3D` itself so the sidebar
  toggle reflects reality. Verified via `--test-disp-preview --test-load <path>` (activates preview on
  the default cube, then loads a real model in the same run — before the fix this reliably reproduced
  the orange cube; after, the new model loads clean and the toggle resets to off).
- Cylinder axis panel (cylindrical-mode UV projection only) is live: a floating inset canvas
  (`ui/cylinder_panel.cpp`, top-left of the viewport, below the status pill) shows a top-down X-Y
  silhouette of the part with a draggable center dot + radius ring, driving
  `settings.cylinderCenterX/Y/cylinderRadius` (all `std::optional<double>`; `nullopt` falls back to
  AABB-derived defaults everywhere — mapping.cpp, preview_material.cpp, smart_resolution.cpp already
  used this `value_or()` pattern before this panel existed). The pure-CPU compute
  (`app::buildCylinderSilhouette` — rasterizes the mesh's X-Y projection into an RGBA buffer with
  50%-margin AABB fit, port of `_buildCylinderSilhouette`; `app::autoFitCylinderAxis` — Kasa
  least-squares circle fit over triangles with `|faceNormal.z| < 0.5`, port of `autoFitCylinderAxis`)
  lives in `app/cylinder_silhouette.{h,cpp}`, kept dependency-free of ModelSession/GL so it's
  trivially testable; `main.cpp` owns the GL texture (uploaded via `render::createTextureRGBA`) and
  rebuilds it lazily — only while mapping mode is Cylindrical AND
  `ModelSession::geometryEpoch()` (bumped on every full-reload `pushGeometry`: fresh load, rotate
  finalize, place-on-face) has advanced since the last build — mirroring main.js's
  `_cylSilhouetteGeometry === currentGeometry` identity check without needing an actual JS-style
  reference-identity comparison. The panel's pan/zoom view transform (mutated by right/middle-drag
  panning and mouse-wheel radius zoom on the ring) is separate ephemeral UI state
  (`ui/cylinder_panel.cpp`'s anonymous-namespace `CylState`) that resets to the silhouette's
  build-time anchor whenever a fresh texture lands. The sidebar's Projection section gained
  Auto-fit-axis / Reset-axis buttons (`ctx.actions.cylinderAutofit`/`cylinderReset`, cylindrical mode
  only) — these mirror main.js's `cylinder-axis-row`, which is separate from the floating canvas
  panel (that only has a minimize toggle).

Phase 7 (app features) is mostly live — native file dialogs, model/texture loading, the
export/bake worker pipeline, undo/redo, `.texturify` project save/load, session autosave, the mesh
diagnostics popup, and the welcome/license/imprint modals + i18n backend are done; the full i18n
string retrofit (data-i18n sweep across the rest of the UI) is the one remaining item (tracked as a
follow-up task).
- `app/file_dialog.{h,cpp}`: thin `GetOpenFileNameA`/`GetSaveFileNameA` wrappers (comdlg32, already
  linked). `ctx.actions.loadModel`/`importCustomTexture` and `exportStl`/`export3mf`'s save-path
  prompt all go through these. No parent HWND is passed (kept decoupled from GLFW/win32 window
  handles) — a minor UX ding (dialog isn't window-modal-parented) accepted for the decoupling.
- `app/pipeline_runner.{h,cpp}` (`PipelineRunner`): std::thread-backed port of
  handleExport/bakeTextures + the exportWorker.js machinery. `start()` snapshots geometry/settings/
  texture/pose by value (so the worker thread never touches ModelSession/GL) and launches the
  thread; `poll()` (called once/frame from main.cpp) copies progress into `state.pipeline*` and, once
  the worker sets `_done` (release/acquire pair — no mutex needed for the result), applies the
  outcome: writes the STL/3MF bytes for Export jobs (already written by the worker thread itself,
  since `core::exportSTL`/`export3MF` + `fwrite` are pure CPU/file-IO, no GL) or calls
  `session.setGeometry()` + the new `ModelSession::seedExcludedFaces()` for Bake jobs (adds the
  "mask just-baked faces" `preExcludedFaces`, gated by `state.bakeKeepMask`, computed from
  `result.faceParentId` + the same combined face-weights used for subdivision). A bake started before
  the model changes underneath it (epoch mismatch) is discarded silently in `poll()`, mirroring
  main.js's `isStale()` check. `buildCombinedFaceWeights()` (angle-mask + user-exclusion combined,
  port of the JS function of the same name) and `toDisplacementSettings()`/`toExportPipelineSettings()`
  (Settings → core structs, shared with the Smart Resolution button) live in this file.
  `restoreOriginalPose()` (undoes in-app rotation before writing the export, port of
  `_restoreOriginalPose`) runs on the worker thread — pure quaternion math, no GL.
- Correctness fix made while wiring this: `app::Settings::amplitude` plays main.js's `textureHeight`
  role (always-positive slider value) — main.js derives the *signed* `settings.amplitude` as
  `(invertDisplacement ? -1 : 1) * textureHeight` at every write site. The "Invert" toggle previously
  had **no effect** in this port (neither the live preview's `buildPreviewParams` nor any pipeline
  path folded the sign in) — fixed at the two point-of-use sites (`buildPreviewParams` in main.cpp,
  `toDisplacementSettings` in pipeline_runner.cpp) rather than restructuring the field, since the UI
  slider is already correctly bound to the magnitude.
- Smart Resolution (`ctx.actions.smartResolution`) and the amplitude/resolution warning banners
  (`panels_right.cpp`) are thin wiring over the already-complete `core::computeSmartResolution` —
  refineLength + maxTriangles are set as a matched pair like `applySmartResolution()`, and the
  warnings re-evaluate every frame the relevant section draws (cheap arithmetic, no debouncing needed
  since ImGui immediate mode already only recomputes-and-draws when the panel is visible).
- `--test-export`/`--test-bake` (see Build/debug-flags above) exercise the exact same
  `PipelineRunner` code path as the real buttons without a blocking native dialog in the loop; used to
  smoke-test STL (binary framing byte-count check) and 3MF (ZIP magic-byte check) output and confirm
  the worker thread completes and reports progress without deadlocking.
- `app/json.{h,cpp}`: a small hand-rolled `JsonValue` (variant-ish class, not a `std::variant`) +
  recursive-descent parser — no vendored JSON library existed in the tree, and the schema here
  (settings snapshots, mask arrays) is flat and fully known, so a focused ~250-line implementation
  beat pulling in a general-purpose one. `dump()` pretty-prints with 2-space indent; numbers use
  `%.17g` (round-trips any double exactly via `strtod`, at the cost of occasionally-ugly output like
  `0.30000000000000004` — correctness over cosmetics, this file is never meant to be hand-edited).
- `app/settings_snapshot.{h,cpp}`: port of main.js's `PERSISTED_KEYS`/`getSettingsSnapshot`/
  `applySettingsSnapshot` — the flat `Settings` subset (excludes the Advanced/Beta Regularize fields
  and `useDisplacement`, exactly like main.js) shared by undo/redo, project files, and autosave.
  `activeMapName`/`activeMapIsCustom` ride alongside it everywhere main.js's snapshot object does.
- `app/undo_stack.{h,cpp}` (`UndoStack`): a debounced (400ms) snapshot stack, not a command stack —
  port of main.js's `_scheduleUndoCapture`/`_commitUndoCapture`/`_undo`/`_redo`. Every
  settings-changing action calls `scheduleCapture()`; `update()` (called once/frame) commits once the
  window elapses. `ModelSession` gained two small additions for this: `collectMaskFaces()` (main.js's
  `_collectCurrentMask` — projects live precision-space selections back to base indices when
  precision masking is active) and `setSelectionModeImmediate()` (main.js's `setSelectionMode` called
  from `_restoreMask` — flips mode + clears both face sets *synchronously*, unlike the UI toggle path
  which only takes effect on the next frame's `update()` diff; undo/redo needs the mode flip and the
  mask reseed to land in the same call, with no frame of lag between them, or the next frame's diff
  would wipe the just-restored mask). `commitBaselineAsStep()`/`rebaseline()` support atomic actions
  (Reset to Defaults) that commit an undo step immediately rather than via the debounce path — see
  `ctx.actions.resetSettings` in main.cpp, a fairly direct port of `resetSettingsToDefaults()`
  (flush pending → push baseline as a step → apply `DEFAULT_SETTINGS_SNAPSHOT` + bounds-derived
  `refineLength` → clear mask → reselect "Crystal" → rebaseline). Ctrl+Z/Ctrl+Y are wired in
  main.cpp's event loop (skipped while `io.WantTextInput`, matching main.js's text-field guard).
  Deviation: `cylinderAutofit`/`cylinderReset`/the cylinder-panel drag all call `scheduleCapture()`
  in this port; main.js's equivalents live outside `#settings-panel` and only call `_autoSaveSettings`
  (never `_scheduleUndoCapture`), so cylinder-axis edits alone never earn their own undo step there —
  a likely oversight in the original rather than an intentional design choice, so this port
  deliberately captures them (a strict UX improvement, not a fidelity break — no mesh output changes).
- `app/session_autosave.{h,cpp}` (`SessionAutosave`): main.js's `_autoSaveSettings`/
  `_restoreSessionSettings`, adapted from sessionStorage (browser-tab-scoped, gone on tab close) to
  `%APPDATA%\Texturify\session.json` — a deliberate improvement, since a native app has no
  "closing the tab" moment to reset from, and surviving a restart is what users expect. Same 300ms
  debounce pattern as `UndoStack`. Restored once at startup, before `--preset`/`--mapping` debug
  overrides so those still win for verification runs.
- `app/project_file.{h,cpp}` (`buildProjectFile`/`parseProjectFile`): the `.texturify` ZIP format
  (settings.json + optional model.stl/mask.json/texture.png), using miniz's reader API alongside the
  writer API `exporter.cpp` already used for 3MF. `model.stl` is written in the model's ORIGINAL
  pre-rotation pose (via the now-public `pipeline_runner.h`'s `restoreOriginalPose`, shared with the
  export pipeline) so re-import + `poseRotation` replay reproduces the working pose — see main.js's
  issue #82 comment. Two **deliberate simplifications** vs. main.js, both driven by this port having
  no modal-dialog infrastructure: (1) export always bundles whatever is available (model + mask when
  a model is loaded, texture.png when a custom texture is active) instead of asking via checkboxes;
  (2) import always replaces the model when the file has one, instead of asking "replace model or
  settings-only?" — main.js defaults to "keep current" specifically because its `currentGeometry` is
  `null` until the user loads something, but this port always has *some* geometry loaded (the default
  cube stands in for "nothing loaded yet"), so that default doesn't translate; always-replace is the
  more useful default for a native "Open" action. **Not yet wired**: `poseRotation` replay on import
  (the rotation is parsed into `ParsedProjectFile::poseRotation` but not yet replayed onto the loaded
  geometry — would need a new `ModelSession` method to apply a raw quaternion post-load, since
  `applyRotation()` only takes incremental Euler-degree deltas). `ctx.actions.newProject` (toolbar
  "New") isn't a main.js concept — implemented as reset-geometry-to-default-cube +
  reset-settings-to-defaults + full undo/autosave rebaseline, the closest native equivalent.
- Mesh diagnostics (port of main.js:3062-3269 + the `#mesh-diagnostics` DOM card,
  style.css:1041-1163): `ModelSession::runFastDiagnosticsInternal()` reruns
  `core::runFastDiagnostics` automatically from `pushGeometry()`'s fullReload branch (every full
  mesh reload — fresh load, rotate finalize, place-on-face, bake adoption — matches every call site
  of main.js's `updateMeshDiagnostics()`) and mirrors the counts into `AppState::diag*` fields, since
  `ui/*.cpp` only sees `AppState`, not `ModelSession`. Advanced checks (intersecting/overlapping
  triangles, `core::runExpensiveDiagnostics`) are O(n²)-ish, so they run on a background thread —
  `app::DiagnosticsRunner` (new, mirrors `PipelineRunner`'s snapshot-by-value threading pattern) —
  triggered by `ctx.actions.runDiagnostics` ("Run Advanced Checks") and polled once/frame in
  main.cpp; `ModelSession::geometryEpoch()` doubles as main.js's `diagToken` — a stale result
  (mesh changed mid-run) is silently discarded by `DiagnosticsRunner::poll()`, and main.cpp calls
  `cancel()` the frame the epoch changes so the worker aborts promptly instead of finishing a scan
  nobody wants. `ModelSession::toggleDiagHighlight(DiagHighlight kind)`/`clearDiagHighlight()` port
  `toggleDiagHighlight`/`clearDiagHighlight` — edges via `core::getEdgePositions` +
  `viewer.setDiagEdges`, shells via `core::getShellAssignments` + per-shell `addDiagFaces` (same
  `SHELL_COLORS` palette), intersects/overlaps via `core::buildExclusionOverlayGeo` over the stored
  face-index lists. The popup itself (`ui/diag_panel.cpp`, `drawDiagnosticsPanel`) ports
  `applyDiagSeverity`'s bottom-left/top-right corner flip (ok stays bottom-left; warn/error jumps to
  the top-right "attention" corner) and `renderFastDiag`/`renderAdvancedDiag`'s per-finding
  Show/Hide rows, sized from precomputed content height (not ImGui auto-resize, to avoid a
  1-frame-stale background-rect chase) so it can be positioned before a pixel is drawn. **Deviation**:
  main.js's icon glyphs (✔/⚠) sit outside the Misc Symbols block, which no loaded font covers even
  after the i18n work below widened the default glyph range (Latin Extended-A + Cyrillic, optionally
  CJK) — adding Misc Symbols too would be a one-off bloat for a single popup — so the popup draws
  small hand-vectored check/triangle icons via `ImDrawList` instead; the em-dash/ellipsis it also
  swapped for plain ASCII would actually render fine now (General Punctuation turned out to be
  bundled into ImGui's "default" range and Noto Sans has real glyphs there — confirmed incidentally
  when the license/imprint modals below rendered real em dashes with no extra work), but the ASCII
  swap was left as-is since there was no reason to revisit already-working code. **Native
  addition**: main.js's only path back to a dismissed popup is reloading the model; this port's
  sidebar "Mesh Diagnostics" section (`ui/panels_right.cpp`) is a compact status dot + a "Show
  details" button that un-dismisses it — main.js has no sidebar entry for diagnostics at all.
- Welcome / License / Imprint modals (port of main.js:1180-1229 + `wireEvents()` ~1526-1541; markup
  `#welcome-overlay`/`#license-overlay`/`#imprint-overlay` in index.html, styles in style.css
  ~.license-overlay/.welcome-overlay): `ui/modals.cpp`'s `drawModals()` (called once from
  `ui::drawUi`) draws all three via `ImGui::BeginPopupModal` — which gives the fixed, centered,
  dimmed backdrop main.js's CSS achieves with `position:fixed` + a flex-centered div, for free — with
  `ImGuiWindowFlags_NoTitleBar` (a hand-drawn heading + `.license-close-btn`-style × replace it) and
  `AlwaysAutoResize` capped by `SetNextWindowSizeConstraints` (so content past ~85% of the viewport
  height scrolls, matching `.imprint-modal`'s `max-height:85vh`). **Deviation**: main.js also closes
  on backdrop click; these are true modals (block interaction with the rest of the app while open)
  dismissed only via the × button, "Close"/"Got it", or Escape — backdrop-click-to-dismiss was judged
  not worth the extra complexity for a native app where an explicit close action is already normal.
  License/imprint bodies are real legal text pulled from the `license.*`/`imprint.*` i18n keys (see
  below) via `app::I18n::t()`; `stripHtml()` reduces their inline HTML (`<strong>`, `<a href>`,
  `<br>`, `<em>`) to plain wrapped text — link text is kept, with the URL appended in parens when the
  visible text doesn't already look like one (e.g. "CNCKitchen.STORE (https://geni.us/…)"), since
  ImGui has no inline rich-text renderer and actually opening URLs would need explicit user
  confirmation per-click anyway. The welcome modal's content had no `data-i18n` at all in the
  original (hardcoded English) — this port initially matched that, but later added real `welcome.*`
  i18n keys (`welcome.title`/`intro`/`inspiredBy`/`quickStart`/`step1-4`/`nativePort`/`bullet1-4`/
  `dontShowAgain`/`gotIt`, `en.json` + `ru.json` only, falling back to English in every other
  language via `t()`'s existing fallback) since the content is this port's own invented text anyway
  (not a literal translation of a main.js string), so authoring it directly in `en.json`/`ru.json`
  isn't "inventing a fake translation" the way it would be for a real original-app string. Its "in
  this release" list was rewritten to describe this native port's own milestones rather than
  literally translating the web app's changelog, since the two ship different feature sets — main.js's version stamp
  (`WELCOME_LAST_UPDATED`) becomes `kWelcomeLastUpdated` in main.cpp, bump it whenever that content
  changes so previously-dismissed users see the popup again. Auto-show-on-first-run
  (`showWelcomeIfNeeded()`) is suppressed for `--screenshot`/`--test-*` runs so those keep verifying
  the real UI underneath rather than a modal; `--test-modal <welcome|license|imprint>` forces one
  open for screenshot verification instead.
- i18n backend (port of `reference/js/i18n.js`; new, from scratch — only the JSON data conversion to
  `assets/lang/*.json` existed before this session): `app::I18n` (`app/i18n.{h,cpp}`) eagerly loads
  every `assets/lang/<code>.json` at startup into a flat `unordered_map<string, unordered_map<string,
  string>>` (unlike main.js's lazy per-language `import()`, which exists there only because fetching
  a JS module over the network is the expensive part — these are small local files, so eager loading
  is simpler and lets the language picker show every language's own native name immediately). `t(key)`
  ports `strings[key] ?? fallback[key] ?? key` (current language → English → the raw key itself);
  `t(key, {{"n","3"}})` adds literal `{name}` substring replacement, matching main.js's
  `replaceAll` (no pluralization/ICU in either). The small `kLanguages` table (`i18n.cpp`, codes +
  each language's own native name) is main.js's `TRANSLATIONS` registry — it isn't part of the JSON
  data in either version. `JsonValue::entries()` (`app/json.h`) was added this session — a small,
  deliberate widening of that class's read API — since i18n's ~230-key documents need generic
  iteration, unlike every other JSON consumer in this codebase (settings snapshots, project files),
  which only ever look up a fixed, known key set via `find()`/`getString()`/etc.
  Persistence (`app::NativePrefs`, `app/native_prefs.{h,cpp}`) is a small standalone
  `%APPDATA%\Texturify\prefs.json` — deliberately *not* folded into `SettingsSnapshot` (undo/redo,
  `.texturify` files, session autosave), since the chosen UI language and the welcome-popup dismiss
  stamp aren't mesh/texture "settings" any more than main.js treats `localStorage['stlt-lang']` and
  `localStorage['stlt-welcome-seen']` as part of its own settings object — both are separate
  top-level keys there for the same reason. `ctx.actions.setLanguage` (main.cpp) calls
  `i18n.setLanguage()` then re-runs `theme.init()` to rebuild the font atlas, then persists.
  **Font atlas / glyph ranges** (`ui::Theme::init`, now takes a `langCode`): every Sans weight always
  merge-loads `NotoSans.ttf` over Latin Extended-A + Cyrillic ranges (built via
  `ImFontGlyphRangesBuilder`), covering 8 of the 10 shipped languages (en/de/it/es/pt/fr/tr/uk) with
  no per-language special-casing at all; `ja`/`ko` additionally merge `NotoSansJP.ttf`/`NotoSansKR.ttf`
  over ImGui's built-in Japanese/Korean glyph ranges (only when that language is actually active, since
  those ranges are far larger — thousands of CJK glyphs vs. ~400 Cyrillic — and baking them
  unconditionally into every atlas would be wasteful). **Runtime language switching rebuilds the font
  atlas** rather than requiring a restart: confirmed safe because `main.cpp` never called any
  explicit `ImGui_ImplOpenGL3_CreateFontsTexture`-style function even for the original startup-only
  `theme.init()` call — this ImGui version (1.92.9)'s OpenGL3 backend re-uploads dirty font textures
  automatically on the next frame via its `ImTextureData` mechanism, so calling `theme.init()` again
  after `io.Fonts->Clear()` + re-adding fonts "just works". Screenshot-verified end-to-end (not just
  "doesn't crash"): the License modal renders real, correctly-shaped Japanese and Ukrainian text with
  zero tofu (`--lang ja --test-modal license`, `--lang uk --test-modal license`), confirming the
  loader, `t()`, and the font merge all compose correctly. `--lang <code>` (main.cpp) forces a
  language at startup for this kind of verification, overriding the saved `prefs.json` preference.
  **Not done**: the mechanical retrofit of the ~150 other hardcoded English strings across the rest
  of `ui/*.cpp` into `t()` calls — only the toolbar's new language-picker/help-menu labels and the
  three modals above actually call `t()` today; everything else (all of `panels_left.cpp`,
  `panels_right.cpp`, `viewport_ui.cpp`, `cylinder_panel.cpp`, `diag_panel.cpp`'s own labels) is still
  hardcoded English regardless of the selected language. This is a large, mechanical, low-risk-if-
  careful sweep rather than a design problem — the infrastructure to do it now exists.
  `--test-load <path>` (see Build/debug-flags above) was added this session to screenshot-verify the
  popup against a mesh with real defects without a file-open dialog in the loop.
- i18n string retrofit, second session: `ui/ui.h` grew a small shared toolkit reused everywhere —
  `T(ctx, key)`/`T(ctx, key, {{"n", val}})` (thin `ctx.i18n->t()` wrapper; safe to call inline as a
  function argument since the returned `std::string` temporary lives through the whole statement —
  NOT safe to stash its `.c_str()` in a variable for a later statement, which is the actual bug this
  port hit once already in `toolbar.cpp`'s language-picker code and caught before it shipped) plus
  `stripInfoIcon()`/`stripWarningIcon()`/`stripCheckIcon()`/`stripHtml()` (all in `ui_shell.cpp`).
  The strip helpers exist because the original's `labels.*ⓘ`/`warnings.*`/`diag.*Ok` strings carry
  glyphs (ⓘ U+24D8, ⚠ U+26A0, ✔ U+2714) outside every loaded font's range — same tofu problem the
  mesh-diagnostics popup hit last session, same fix (drop the decorative glyph; severity/hover state
  is already conveyed by color and layout).
  Retrofitted: `panels_right.cpp` (Projection/Transform/Texture Depth/Masking/Resolution sections,
  the export/bake footer, the sidebar diagnostics status line), `panels_left.cpp` (Custom Map,
  Texture Smoothing, Load Model, rotate Apply/Reset, Place on Face), `cylinder_panel.cpp` (empty-state
  message, panel label), `diag_panel.cpp` (fully — `buildFastLines`/`buildAdvancedLines` now take
  `UiContext&` and interpolate `diag.openEdges`/`diag.nonManifoldEdges`/`diag.multipleShells`/
  `diag.intersectingTris`/`diag.overlappingTris`/`diag.meshOk`/`diag.advancedOk` via `{n}` instead of
  hand-rolled `snprintf`, and the Show/Hide/Run-Advanced-Checks/Checking labels and the
  `diag.recommendFix` tip — the last one round-tripped through `stripHtml()`, confirmed by screenshot
  to correctly turn the source's `<a href>` into "online (https://...)"). `modals.cpp`'s license/
  imprint bodies were already on `t()` from the first i18n session.
  A few labels changed wording to match the (more descriptive) original during this pass — e.g.
  "Displacement" section → "Texture Depth", "Amplitude" → "Texture height (mm)", "UV Transform" →
  "Transform", cylinder "Auto-fit axis"/"Reset axis" → "Auto-fit"/"Reset" — these are intentional
  fidelity corrections, not regressions.
  **Left hardcoded, deliberately**: anything with no corresponding key in the original 233-key
  `en.json` at all, because inventing a key and typing an English-only string into 10 language files
  would be a fake translation, not a real one. This covers: all native-only app chrome (toolbar
  New/Open/Save/Undo/Redo/Load model, the Live/Baked status pill, `viewport_ui.cpp`'s Solid/
  Wireframe/Textured chips, the "Regularize advanced" sub-sliders, the "Resolution & Quality" and
  "Mesh Diagnostics" section headings, "Rotation gizmo", "Lock U/V"); the pipeline's progress-stage
  strings (`app/pipeline_runner.cpp` sets `state.pipelineStage` from a **worker thread** — routing
  that through `app::I18n::t()` would read `i18n`'s language-switch state without synchronization,
  a real race with `ctx.actions.setLanguage`'s font-atlas rebuild on the main thread, so it was left
  alone rather than introduced as a new thread-safety bug); and the Single/Radius/Bucket brush-tool
  chip row (translating only "Bucket"→"Fill" while "Single"/"Radius" stay English, because only two
  of the three have a matching original key, would look worse than leaving the row consistently
  English). All of these are candidates for new `en.json`-only keys (falling back to English in every
  other language via `t()`'s existing fallback) if a future pass wants full coverage.
- **Renamed BumpMesh → Texturify** (product/branding rename; not a fidelity change — the mesh
  pipeline is untouched): CMake targets (`bumpmesh`/`bumpmesh_core`/`bumpmesh_verify` →
  `texturify`/`texturify_core`/`texturify_verify`), the window title, toolbar wordmark, welcome-modal
  title, project-file extension (`.bumpmesh` → `.texturify`, both the save/open dialog filters and
  `header.exportProject`/`importProject` in every `assets/lang/*.json`), and the native prefs/session
  autosave folder (`%APPDATA%\BumpMesh` → `%APPDATA%\Texturify`). The welcome modal and README now
  carry an explicit "Texturify is inspired by BumpMesh (CNC Kitchen's stlTexturizer) — an independent
  native rewrite, not the original project" note. **Deliberately left alone**: the License/Imprint
  modal bodies (`license.*`/`imprint.*` i18n keys) — these are the original work's actual AGPL
  license terms and legal contact details, required attribution, not "BumpMesh branding" to scrub.
  `reference/` and `third_party/` (vendored/upstream code) were not touched.
- **Added Russian (`ru`), made it the default language**: `assets/lang/ru.json` (233 keys, full parity
  with `en.json`, hand-translated — not machine-translated placeholder text), `"ru"` added to
  `kLanguages` in `app/i18n.cpp` with native name "Русский". `I18n::init()` now defaults `_current` to
  `"ru"` (falling back to `"en"` only if `ru.json` somehow failed to load) instead of the previous
  hardcoded `"en"` — English remains the ultimate per-key fallback inside `t()` regardless. No font
  atlas changes needed: `ui::Theme::init()` already merges Cyrillic ranges unconditionally for every
  language (added when Ukrainian shipped), so Russian renders correctly out of the box.
  **Bug found and fixed while verifying this**: `ui::sectionHeader()` (`widgets.cpp`) uppercased and
  drew its label **byte-by-byte** (`char one[2] = {buf[i], 0}` per byte) for its manual-letterspacing
  effect — correct for ASCII, but it silently corrupted every multi-byte UTF-8 string (each byte
  drawn as its own invalid 1-byte "codepoint," rendering as repeated tofu/diamond glyphs). This
  affected every section heading (PROJECTION, TEXTURE DEPTH, MASKING, ...) in **any** non-Latin
  language — Russian surfaced it immediately since it's used everywhere by default now, but Ukrainian
  had the identical latent bug the whole time (nobody had screenshotted `sectionHeader` specifically
  in `uk` before). Fixed by iterating per UTF-8 codepoint (`utf8SeqLen()` helper) instead of per byte:
  ASCII bytes still get `toupper()`'d (preserving the existing all-caps look for en/de/fr/...),
  multi-byte sequences are copied through unchanged rather than case-folded (matching how CJK already
  behaved here, since ASCII `toupper` was always a no-op on CJK bytes) or corrupted. Verified via
  screenshot: Russian and Ukrainian section headers both render clean, readable Cyrillic post-fix.
- **Welcome modal translated too**: added real `welcome.*` i18n keys (see the i18n section above)
  instead of leaving it hardcoded English forever — screenshot-verified in both `ru` (default) and
  `en` (fallback).
- **App icon** (`assets/icon/`, generated from user-supplied artwork via Pillow —
  `source.png` kept for regeneration): `app.ico` (16/24/32/48/64/128/256, embedded via
  `assets/icon/app.rc`'s `101 ICON "app.ico"`, added to `add_executable(texturify ...)` in
  CMakeLists.txt) is the Explorer/Alt-Tab executable icon; `icon_{16,32,48,64,128}.png` are decoded
  at startup (`core::decodeImageFileRGBA`) and passed to `glfwSetWindowIcon` for the live
  window/taskbar icon (main.cpp, right after `resolveAssetDir()`); `logo_64.png` replaces the
  toolbar's procedural gradient chip (`ui::UiContext::logoTex`, loaded alongside the preset
  thumbnails, drawn via `AddImageRounded` in `toolbar.cpp` — falls back to the original gradient
  chip if the asset fails to load). Verified the `.ico` embedding actually took by extracting it
  back out of the built `texturify.exe` with `System.Drawing.Icon.ExtractAssociatedIcon` in
  PowerShell, not just trusting a clean MSBuild log.
- **Model surface color, teal → red**: `render/preview_material.cpp`'s fragment shader had a
  `tealBase = vec3(0.22, 0.68, 0.68)` constant for the non-masked/"textured" surface color (reading
  as pale green/cyan to the user) — renamed to `baseColor`/`litBase` and changed to
  `vec3(0.75, 0.16, 0.18)` (a muted red, same overall lightness as the old teal so the lighting/
  specular math didn't need retuning), chosen to stay clearly distinct from `userMaskColor`
  (orange, excluded surfaces) and `angleMaskColor` (grey). Purely a preview-shader constant — doesn't
  touch the export/bake pipeline's own texture-color handling.
- **Custom window chrome, replacing the native Windows title bar** (`app/custom_chrome.{h,cpp}`,
  new `CustomChrome` class; Windows-only, no-op stubs on other platforms): the toolbar (already a
  glass strip across the top) now doubles as the OS-native draggable caption region, so the default
  white/system title bar is gone entirely — not just recolored. Uses the standard "extend client area
  over the whole window, then re-add caption/resize hit-testing in WM_NCHITTEST" technique: the
  window keeps its `WS_CAPTION|WS_THICKFRAME` styles (so Aero Snap, resize cursors, and double-click-
  to-maximize all keep working natively, for free), only its non-client PAINT area is suppressed.
  **The key subtlety, caught via real interactive testing** (see below): `WM_NCCALCSIZE` must NOT
  call through to `DefWindowProc`/the original window proc for the normal (non-maximized) case — doing
  so lets the default handler shrink the client rect back down to the standard size, silently
  reserving space for (and still painting) the native title bar, so the first build looked unchanged
  even though the code "looked right." The fix is to leave `NCCALCSIZE_PARAMS::rgrc[0]` completely
  untouched (return 0 without modifying it) for the normal case — that alone makes the whole window
  client area with nothing left to paint a title bar into. The maximized case still needs the classic
  correction (inset by `SM_CXSIZEFRAME + SM_CXPADDEDBORDER` on each side), or a maximized window
  overhangs the monitor by the invisible resize-frame thickness. `WM_NCHITTEST` calls through to the
  original proc first (so real edge/corner resize detection stays correct), and only overrides an
  `HTCLIENT` result to `HTCAPTION` when the point falls in the toolbar's height band AND isn't inside
  one of `ui::UiContext::dragExemptRects` — the screen-space rects of the toolbar's own real controls
  (New/Open/Save/Undo/Redo/Load model/language picker/help/status pill/Export/the new window-control
  buttons themselves), rebuilt every frame in `toolbar.cpp` via a `registerExempt()` helper called
  after each widget, fed to `chrome.updateHitTestRegion()` right after `ui::drawUi()` in main.cpp's
  frame loop. Minimize/maximize/close are hand-vectored icons (same rationale as the diagnostics
  popup's icons — no font glyph dependency) drawn flush against the true window edge bypassing the
  toolbar's own padding (same technique as `modals.cpp`'s `drawCloseX`); maximize/restore swaps
  between a single-square and double-square icon based on `ui::UiContext::windowMaximized`
  (`chrome.isMaximized()`, refreshed once per frame before `drawUi()`); close's hover background uses
  Windows' own close-hover red (`#E81123`) rather than the app's accent, matching platform convention.
  **Verified with real interactive testing, not just framebuffer screenshots** — `--screenshot` only
  captures the OpenGL framebuffer, which can't show whether the *native OS* title bar is actually
  gone, so this needed an actual on-screen window: confirmed via computer-use screenshots that the
  native title bar is genuinely absent (first attempt still showed it — see the WM_NCCALCSIZE bug
  above, only caught because this step wasn't skipped) and that dragging the toolbar's empty space
  moves the window through the real OS drag path (`HTCAPTION`), not a simulated one.
