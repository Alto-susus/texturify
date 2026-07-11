// Texturify — native port of CNC Kitchen stlTexturizer (AGPL-3.0)
// Inspired by / originally derived from BumpMesh.
// Original: https://github.com/CNCKitchen/stlTexturizer  © Stefan Hermann / CNC Kitchen
#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif
#include <chrono>
#include <filesystem>

#include "render/gl_loader.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "app/actions.h"
#include "app/app_state.h"
#include "app/custom_chrome.h"
#include "app/cylinder_silhouette.h"
#include "app/diagnostics_runner.h"
#include "app/file_dialog.h"
#include "app/i18n.h"
#include "app/mac_bundle_path.h"
#include "app/native_prefs.h"
#include "app/pipeline_runner.h"
#include "app/preset_textures.h"
#include "app/preview_attributes.h"
#include "app/project_file.h"
#include "app/session_autosave.h"
#include "app/undo_stack.h"
#include "core/exporter.h"
#include "core/geometry.h"
#include "core/image.h"
#include "core/loaders.h"
#include "core/smart_resolution.h"
#include "render/viewer.h"
#include "ui/glass.h"
#include "ui/theme.h"
#include "ui/ui.h"

int main(int, char**);

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return main(__argc, __argv); }
#endif

namespace {

double g_scrollY = 0; // accumulated GLFW wheel, drained each frame

// Version stamp for the "What's New" auto-popup (main.js's
// WELCOME_LAST_UPDATED) — bump this whenever the welcome modal's changelog
// content changes, so users who dismissed an earlier version see it again.
constexpr const char* kWelcomeLastUpdated = "cpp-port-2026-07-11";

// Prefer assets next to the executable (post-build copy), fall back to the
// working directory (running from the project root).
std::string resolveAssetDir() {
#if defined(_WIN32)
  char buf[MAX_PATH];
  DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  if (n > 0 && n < MAX_PATH) {
    std::string dir(buf, n);
    size_t slash = dir.find_last_of("/\\");
    if (slash != std::string::npos) {
      dir = dir.substr(0, slash) + "\\assets";
      DWORD attrs = GetFileAttributesA(dir.c_str());
      if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
        return dir;
    }
  }
#elif defined(__APPLE__)
  // Packaged as a .app bundle: assets live in Contents/Resources/assets
  // (see CMakeLists.txt's macOS post-build copy step).
  std::string bundlePath = app::macBundleResourcePath();
  if (!bundlePath.empty()) {
    std::filesystem::path dir = std::filesystem::path(bundlePath) / "assets";
    std::error_code ec;
    if (std::filesystem::is_directory(dir, ec)) return dir.string();
  }
#elif defined(__linux__)
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) {
    std::filesystem::path dir =
        std::filesystem::path(std::string(buf, (size_t)n)).parent_path() / "assets";
    std::error_code ec;
    if (std::filesystem::is_directory(dir, ec)) return dir.string();
  }
  {
    // .deb install location (CMakeLists.txt's `install(DIRECTORY assets/ ...)`)
    // — the binary lives in /usr/bin, with no assets/ dir beside it there.
    std::error_code ec;
    if (std::filesystem::is_directory("/usr/share/texturify/assets", ec))
      return "/usr/share/texturify/assets";
  }
#endif
  return "assets";
}

// "C:/models/Vase.stl" -> "Vase" ; "C:/models/Vase.stl" (name+ext) -> "Vase.stl"
std::string fileBaseName(const std::string& path, bool stripExt) {
  size_t slash = path.find_last_of("/\\");
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  if (stripExt) {
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
  }
  return name;
}

// 50 mm cube centered on the origin (the app's default model — matches
// main.js's loadDefaultCube(), `new THREE.BoxGeometry(50, 50, 50)`).
core::Geometry makeDefaultCube() {
  core::Geometry g;
  const float h = 25.0f;
  const float corners[8][3] = {{-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h},
                               {-h, -h, h},  {h, -h, h},  {h, h, h},  {-h, h, h}};
  // quads (a,b,c,d) with outward normal n
  const int quads[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
                           {2, 3, 7, 6}, {1, 2, 6, 5}, {3, 0, 4, 7}};
  const float normals[6][3] = {{0, 0, -1}, {0, 0, 1},  {0, -1, 0},
                               {0, 1, 0},  {1, 0, 0},  {-1, 0, 0}};
  for (int f = 0; f < 6; f++) {
    const int tri[6] = {quads[f][0], quads[f][1], quads[f][2],
                        quads[f][0], quads[f][2], quads[f][3]};
    for (int v = 0; v < 6; v++) {
      for (int k = 0; k < 3; k++) g.positions.push_back(corners[tri[v]][k]);
      for (int k = 0; k < 3; k++) g.normals.push_back(normals[f][k]);
    }
  }
  return g;
}

// Mirror of main.js updatePreview(): settings + bounds + texture aspect
// (the wider texture axis gets aspect 1, the other tiles faster).
render::PreviewParams buildPreviewParams(const app::AppState& st,
                                         const core::Bounds& bounds,
                                         int texW, int texH) {
  const app::Settings& s = st.settings;
  render::PreviewParams p;
  p.mappingMode = s.mappingMode;
  p.scaleU = s.scaleU;
  p.scaleV = s.scaleV;
  // main.js: settings.amplitude = (invertDisplacement ? -1 : 1) * textureHeight
  // — s.amplitude here plays textureHeight's role (always-positive slider);
  // fold the sign in the same way at the point of use.
  p.amplitude = s.invertDisplacement ? -s.amplitude : s.amplitude;
  p.offsetU = s.offsetU;
  p.offsetV = s.offsetV;
  p.rotation = s.rotation;
  p.bounds = bounds;
  p.hasBounds = true;
  p.cylinderCenterX = s.cylinderCenterX;
  p.cylinderCenterY = s.cylinderCenterY;
  p.cylinderRadius = s.cylinderRadius;
  p.bottomAngleLimit = s.bottomAngleLimit;
  p.topAngleLimit = s.topAngleLimit;
  p.mappingBlend = s.mappingBlend;
  p.seamBandWidth = s.seamBandWidth;
  p.capAngle = s.capAngle;
  p.symmetricDisplacement = s.symmetricDisplacement;
  p.noDownwardZ = s.noDownwardZ;
  p.useDisplacement = st.displacementPreview3D;
  p.boundaryFalloff = s.boundaryFalloff;
  double tmax = std::max({(double)texW, (double)texH, 1.0});
  p.textureAspectU = tmax / std::max(texW, 1);
  p.textureAspectV = tmax / std::max(texH, 1);
  return p;
}

} // namespace

int main(int argc, char** argv) {
  // --screenshot <path>: render a few frames, dump the framebuffer, exit.
  // Used for UI verification against the mockup.
  const char* screenshotPath = nullptr;
  int startPreset = -1; // --preset <idx>: pre-select a texture (verification)
  int startMapping = -1; // --mapping <idx>: pre-select a mapping mode
  double startAmplitude = -1; // --amplitude <mm>: override texture height at startup (verification)
  // --test-export <path.stl|path.3mf>: bypass the save dialog and run a real
  // export through PipelineRunner (bit-for-bit the button's code path),
  // polling to completion, then exit — used to smoke-test the worker thread
  // without a modal Win32 dialog in the loop.
  const char* testExportPath = nullptr;
  bool testBake = false; // --test-bake: same, but runs the bake job instead
  bool testUndo = false; // --test-undo: exercises undo/redo + JSON round-trip, exits
  bool testDispPreview = false; // --test-disp-preview: force 3D displacement preview on at startup (requires --preset; screenshot verification)
  bool startWireframe = false; // --wireframe: force wireframe view at startup (screenshot verification)
  const char* testProjectPath = nullptr; // --test-project <path.texturify>: save+reload round-trip
  const char* testLoadPath = nullptr; // --test-load <path.stl|obj|3mf>: load a model at startup, bypassing the file dialog (QA/screenshot use)
  const char* startLang = nullptr; // --lang <code>: force UI language at startup (verification)
  const char* testModal = nullptr; // --test-modal <welcome|license|imprint>: force that modal open (screenshot verification)
  for (int i = 1; i + 1 < argc; i++) {
    if (std::strcmp(argv[i], "--screenshot") == 0) screenshotPath = argv[i + 1];
    if (std::strcmp(argv[i], "--preset") == 0) startPreset = std::atoi(argv[i + 1]);
    if (std::strcmp(argv[i], "--mapping") == 0) startMapping = std::atoi(argv[i + 1]);
    if (std::strcmp(argv[i], "--amplitude") == 0) startAmplitude = std::atof(argv[i + 1]);
    if (std::strcmp(argv[i], "--test-export") == 0) testExportPath = argv[i + 1];
    if (std::strcmp(argv[i], "--test-project") == 0) testProjectPath = argv[i + 1];
    if (std::strcmp(argv[i], "--test-load") == 0) testLoadPath = argv[i + 1];
    if (std::strcmp(argv[i], "--lang") == 0) startLang = argv[i + 1];
    if (std::strcmp(argv[i], "--test-modal") == 0) testModal = argv[i + 1];
  }
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--test-bake") == 0) testBake = true;
    if (std::strcmp(argv[i], "--test-undo") == 0) testUndo = true;
    if (std::strcmp(argv[i], "--test-disp-preview") == 0) testDispPreview = true;
    if (std::strcmp(argv[i], "--wireframe") == 0) startWireframe = true;
  }

  if (!glfwInit()) return 1;
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  // UI scale from the primary monitor's content scale.
  float uiScale = 1.0f;
  {
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    if (mon) {
      float sx = 1, sy = 1;
      glfwGetMonitorContentScale(mon, &sx, &sy);
      uiScale = sx > 0 ? sx : 1.0f;
    }
  }

  GLFWwindow* window = glfwCreateWindow((int)(1380 * uiScale),
                                        (int)(864 * uiScale), "Texturify",
                                        nullptr, nullptr);
  if (!window) { glfwTerminate(); return 1; }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  if (!glLoaderInit()) { std::fprintf(stderr, "GL loader failed\n"); return 1; }

  // Custom window chrome: suppresses the native title bar's paint while
  // keeping native move/resize/snap/maximize (see app/custom_chrome.h) —
  // the toolbar below acts as the replacement title bar.
  app::CustomChrome chrome;
  chrome.install(window);

  // Install before ImGui's backend so it chains to us.
  glfwSetScrollCallback(window, [](GLFWwindow*, double, double y) { g_scrollY += y; });

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr; // fixed docked layout — nothing to save
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  const std::string assetDir = resolveAssetDir();

  // Window/taskbar icon (assets/icon/icon_*.png, generated from the app
  // artwork). The .exe's own icon (Explorer/Alt-Tab) comes separately from
  // assets/icon/app.rc, embedded at link time — GLFW's icon only affects the
  // live window/taskbar entry.
  {
    std::vector<core::ImageDataRGBA> iconImages;
    for (int sz : {16, 32, 48, 64, 128}) {
      core::ImageDataRGBA img = core::decodeImageFileRGBA(
          assetDir + "/icon/icon_" + std::to_string(sz) + ".png");
      if (!img.data.empty()) iconImages.push_back(std::move(img));
    }
    std::vector<GLFWimage> glfwIcons;
    for (core::ImageDataRGBA& img : iconImages)
      glfwIcons.push_back({img.width, img.height, img.data.data()});
    if (!glfwIcons.empty())
      glfwSetWindowIcon(window, (int)glfwIcons.size(), glfwIcons.data());
  }

  app::I18n i18n;
  if (!i18n.init(assetDir))
    std::fprintf(stderr, "i18n init: failed to load English strings under %s\\lang\n",
                 assetDir.c_str());
  app::NativePrefs nativePrefs = app::loadNativePrefs();
  if (startLang) nativePrefs.lang = startLang; // --lang overrides the saved preference
  if (!nativePrefs.lang.empty()) i18n.setLanguage(nativePrefs.lang);

  ui::Theme theme;
  if (!theme.init(assetDir, uiScale, i18n.currentLanguage()))
    std::fprintf(stderr, "theme init: missing fonts under %s\\fonts\n",
                 assetDir.c_str());

  ui::GlassCompositor glass;
  if (!glass.init())
    std::fprintf(stderr, "glass compositor init reported shader errors\n");

  render::Viewer viewer;
  if (!viewer.init(assetDir))
    std::fprintf(stderr, "viewer init reported shader errors\n");
  viewer.setSceneBackground(0x08080b); // mockup viewport bg

  app::AppState state;
  app::ModelSession session(state, viewer);
  session.setGeometry(makeDefaultCube(), "cube_50x50x50");

  // Active displacement texture (owned here; viewer only references it)
  GLuint displacementTex = 0;
  int texW = 1, texH = 1;
  std::optional<app::TextureEntry> customTextureEntry; // set by importCustomTexture
  GLuint customThumbTex = 0;

  app::PipelineRunner pipelineRunner;
  app::DiagnosticsRunner diagRunner;
  app::UndoStack undoStack;
  app::SessionAutosave autosave;
  int diagEpochSeen = session.geometryEpoch(); // cancels diagRunner on mesh change

  ui::UiContext ctx;
  ctx.state = &state;
  ctx.theme = &theme;
  ctx.glass = &glass;
  ctx.i18n = &i18n;

  // Cylinder-axis silhouette texture (owned here; rebuilt lazily in the main
  // loop whenever cylindrical mode is active and the base mesh topology has
  // changed since the last build — see session.geometryEpoch()).
  GLuint cylinderSilhouetteTex = 0;
  auto rebuildCylinderSilhouette = [&]() {
    app::CylinderSilhouette sil =
        app::buildCylinderSilhouette(session.geometry(), session.bounds(), 240, 240);
    if (cylinderSilhouetteTex) glDeleteTextures(1, &cylinderSilhouetteTex);
    cylinderSilhouetteTex = render::createTextureRGBA(
        sil.rgba.data(), sil.width, sil.height, /*repeat=*/false,
        /*linear=*/true, /*mipmaps=*/false, /*flipY=*/false);
    ctx.cylinderSilhouette.valid = !session.geometry().positions.empty();
    ctx.cylinderSilhouette.tex = (ImTextureID)(intptr_t)cylinderSilhouetteTex;
    ctx.cylinderSilhouette.width = sil.width;
    ctx.cylinderSilhouette.height = sil.height;
    ctx.cylinderSilhouette.cxw = sil.cxw;
    ctx.cylinderSilhouette.cyw = sil.cyw;
    ctx.cylinderSilhouette.scale = sil.scale;
    ctx.cylinderSilhouette.geometryEpoch = session.geometryEpoch();
  };

  // Preset thumbnails (80x80, generated from the full images)
  for (int i = 0; i < app::kPresetTextureCount; i++) {
    const app::TextureEntry* e = app::loadFullPreset(i, assetDir);
    if (!e) continue;
    core::ImageDataRGBA thumb = app::makeThumbnail(e->image);
    GLuint t = render::createTextureRGBA(thumb.data.data(), thumb.width,
                                         thumb.height, /*repeat=*/false,
                                         /*linear=*/true, /*mipmaps=*/true,
                                         /*flipY=*/false);
    ctx.presetThumbs[i] = (ImTextureID)(intptr_t)t;
  }

  // Toolbar logo mark (assets/icon/logo_64.png)
  {
    core::ImageDataRGBA logo = core::decodeImageFileRGBA(assetDir + "/icon/logo_64.png");
    if (!logo.data.empty()) {
      GLuint t = render::createTextureRGBA(logo.data.data(), logo.width, logo.height,
                                           /*repeat=*/false, /*linear=*/true,
                                           /*mipmaps=*/true, /*flipY=*/false);
      ctx.logoTex = (ImTextureID)(intptr_t)t;
    }
  }

  // ── UI actions ────────────────────────────────────────────────────────────
  auto applyPreview = [&]() {
    bool active = state.hasTexture() && state.previewEnabled;
    viewer.setPreviewEnabled(active);
    if (active)
      viewer.setPreviewParams(
          buildPreviewParams(state, session.bounds(), texW, texH));
  };
  session.onGeometryChanged = applyPreview;
  session.onMaskChanged = applyPreview;

  // Shared by selectPreset/importCustomTexture/re-selecting the custom tile
  // /undo-redo's preset-by-name restore. applyDefaults=false skips the
  // scaleU/V-to-defaultScale reset (main.js's selectPreset(idx, el, false)).
  auto applyTextureEntry = [&](const app::TextureEntry& e, bool applyDefaults) {
    if (displacementTex) glDeleteTextures(1, &displacementTex);
    displacementTex = render::createTextureRGBA(
        e.image.data.data(), e.image.width, e.image.height, /*repeat=*/true);
    texW = e.image.width;
    texH = e.image.height;
    viewer.setDisplacementTexture(displacementTex);
    if (applyDefaults) state.settings.scaleU = state.settings.scaleV = e.defaultScale;
    state.meshDirty = true;
    applyPreview();
  };

  auto presetIndexByName = [&](const std::string& name) -> int {
    for (int i = 0; i < app::kPresetTextureCount; i++)
      if (name == app::kTexturePresets[i].name) return i;
    return -1;
  };

  ctx.actions.selectPreset = [&](int idx) {
    if (idx < 0) {
      // Re-select the already-imported custom texture (left-panel tile click).
      if (!customTextureEntry) return;
      state.selectedPreset = -1;
      applyTextureEntry(*customTextureEntry, /*applyDefaults=*/true);
      return;
    }
    const app::TextureEntry* e = app::loadFullPreset(idx, assetDir);
    if (!e) return;
    state.selectedPreset = idx;
    applyTextureEntry(*e, /*applyDefaults=*/true);
  };

  ctx.actions.importCustomTexture = [&]() {
    auto path = app::showOpenFileDialog(
        "Load displacement map", "Image files",
        "*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif;*.psd;*.hdr;*.pic");
    if (!path) return;
    auto entry = app::loadCustomTexture(*path);
    if (!entry) return;
    customTextureEntry = std::move(entry);
    if (customThumbTex) glDeleteTextures(1, &customThumbTex);
    core::ImageDataRGBA thumb = app::makeThumbnail(customTextureEntry->image);
    customThumbTex = render::createTextureRGBA(thumb.data.data(), thumb.width,
                                               thumb.height, /*repeat=*/false,
                                               /*linear=*/true, /*mipmaps=*/true,
                                               /*flipY=*/false);
    ctx.customThumb = (ImTextureID)(intptr_t)customThumbTex;
    state.selectedPreset = -1;
    state.customTextureName = fileBaseName(*path, /*stripExt=*/false);
    applyTextureEntry(*customTextureEntry, /*applyDefaults=*/true);
  };

  ctx.actions.removeCustomTexture = [&]() {
    customTextureEntry.reset();
    state.customTextureName.clear();
    if (state.selectedPreset < 0) {
      if (displacementTex) { glDeleteTextures(1, &displacementTex); displacementTex = 0; }
      texW = texH = 1;
      viewer.setDisplacementTexture(0);
      state.meshDirty = true;
      applyPreview();
    }
  };

  ctx.actions.loadModel = [&]() {
    auto path = app::showOpenFileDialog("Load model", "STL/OBJ/3MF files",
                                        "*.stl;*.obj;*.3mf");
    if (!path) return;
    core::LoadResult result = core::loadModelFile(*path);
    if (!result.ok) return;
    session.setGeometry(std::move(result.geometry), fileBaseName(*path, /*stripExt=*/true));
    state.meshDirty = true;
    applyPreview();
    // Mask indices reference the freshly-loaded triangle set — any prior
    // history is meaningless for the new geometry (main.js's handleModelFile
    // finally block).
    undoStack.reset(state, session);
  };

  ctx.actions.settingsChanged = [&]() {
    state.meshDirty = true;
    applyPreview();
    undoStack.scheduleCapture();
    autosave.scheduleSave();
  };

  ctx.actions.maskSettingsChanged = [&]() {
    session.markFalloffDirty();
    session.updateFaceMask(); // recomputes boundary falloff/edges, re-uploads
    undoStack.scheduleCapture();
    autosave.scheduleSave();
  };

  ctx.actions.viewChanged = [&]() {
    viewer.setWireframe(state.wireframe);
    viewer.setProjection(state.perspectiveProjection);
    applyPreview();
  };

  ctx.actions.toggleRotateGizmo = [&](bool on) { session.setRotateMode(on); };

  ctx.actions.applyRotation = [&](double x, double y, double z) {
    session.applyRotation(x, y, z);
    state.meshDirty = true;
  };
  ctx.actions.resetRotation = [&]() { session.resetRotation(); };
  ctx.actions.placeOnFace = [&](bool active) { session.setPlaceOnFace(active); };
  ctx.actions.clearExclusions = [&]() {
    session.clearExclusions();
    undoStack.scheduleCapture();
    autosave.scheduleSave();
  };
  ctx.actions.togglePrecisionMasking = [&](bool on) {
    session.setPrecisionMasking(on);
  };
  ctx.actions.refreshPrecisionMesh = [&]() { session.refreshPrecisionMesh(); };
  ctx.actions.toggleDisplacementPreview = [&](bool on) {
    session.setDisplacementPreview(on);
  };

  ctx.actions.runDiagnostics = [&]() {
    if (state.diagAdvancedRunning) return;
    if (diagRunner.start(session.geometry(), session.geometryEpoch()))
      state.diagAdvancedRunning = true;
  };
  ctx.actions.dismissDiagnostics = [&]() {
    state.diagDismissed = true;
    session.clearDiagHighlight();
  };
  ctx.actions.toggleDiagHighlight = [&](app::DiagHighlight kind) {
    session.toggleDiagHighlight(kind);
  };

  ctx.actions.setLanguage = [&](const std::string& code) {
    if (!i18n.setLanguage(code)) return;
    theme.init(assetDir, uiScale, code); // rebuilds the font atlas for the new glyph ranges
    nativePrefs.lang = code;
    app::saveNativePrefs(nativePrefs);
  };
  ctx.actions.closeWelcome = [&](bool dontShowAgain) {
    if (state.welcomeAllowDismissPersist && dontShowAgain) {
      nativePrefs.welcomeSeenVersion = kWelcomeLastUpdated;
      app::saveNativePrefs(nativePrefs);
    }
  };

  ctx.actions.minimizeWindow = [&]() { chrome.minimize(); };
  ctx.actions.toggleMaximizeWindow = [&]() { chrome.toggleMaximize(); };
  ctx.actions.closeWindow = [&]() { chrome.close(); };

  ctx.actions.cylinderAutofit = [&]() {
    double cx, cy, r;
    if (app::autoFitCylinderAxis(session.geometry(), session.faceNormalAttr(),
                                 session.bounds(), cx, cy, r)) {
      state.settings.cylinderCenterX = cx;
      state.settings.cylinderCenterY = cy;
      state.settings.cylinderRadius = r;
      state.meshDirty = true;
      applyPreview();
      undoStack.scheduleCapture();
      autosave.scheduleSave();
    }
  };
  ctx.actions.cylinderReset = [&]() {
    state.settings.cylinderCenterX.reset();
    state.settings.cylinderCenterY.reset();
    state.settings.cylinderRadius.reset();
    state.meshDirty = true;
    applyPreview();
    undoStack.scheduleCapture();
    autosave.scheduleSave();
  };

  // Preset cache entries are pointer-stable (app::loadFullPreset caches by
  // index); the custom entry lives in customTextureEntry above.
  auto activeTextureEntry = [&]() -> const app::TextureEntry* {
    if (state.selectedPreset >= 0) return app::loadFullPreset(state.selectedPreset, assetDir);
    if (customTextureEntry) return &*customTextureEntry;
    return nullptr;
  };

  // Port of resetSettingsToDefaults(): flush any pending debounced edit,
  // push it as a dedicated undo step (so Ctrl+Z restores everything the
  // reset just clobbered), apply defaults + refineLength-from-bounds, clear
  // the mask, reselect the default "Crystal" preset, then rebaseline so the
  // reset itself becomes one atomic undo step.
  ctx.actions.resetSettings = [&]() {
    undoStack.flush(state, session);
    undoStack.commitBaselineAsStep();

    app::SettingsSnapshot defaults; // struct defaults already match main.js's DEFAULT_SETTINGS_SNAPSHOT
    const core::Bounds& b = session.bounds();
    const double diag = std::sqrt(b.size.x * b.size.x + b.size.y * b.size.y + b.size.z * b.size.z);
    if (diag > 0) {
      const double v = std::round(diag / 250.0 * 100.0) / 100.0;
      defaults.refineLength = std::max(0.05, std::min(5.0, v));
    }
    app::applySettingsSnapshot(state, defaults);
    session.setSelectionModeImmediate(false);
    session.clearExclusions();

    const int defaultIdx = presetIndexByName("Crystal");
    if (defaultIdx >= 0) {
      const app::TextureEntry* e = app::loadFullPreset(defaultIdx, assetDir);
      if (e) {
        state.selectedPreset = defaultIdx;
        applyTextureEntry(*e, /*applyDefaults=*/true);
      }
    }
    state.meshDirty = true;
    applyPreview();
    undoStack.rebaseline(state, session);
    autosave.scheduleSave();
  };

  ctx.actions.undo = [&]() {
    if (!undoStack.undo(state, session)) return;
    if (undoStack.pendingPresetName) {
      const int idx = presetIndexByName(*undoStack.pendingPresetName);
      const app::TextureEntry* e = idx >= 0 ? app::loadFullPreset(idx, assetDir) : nullptr;
      if (e) {
        state.selectedPreset = idx;
        applyTextureEntry(*e, /*applyDefaults=*/false);
      }
      undoStack.pendingPresetName.reset();
    }
    applyPreview();
    autosave.scheduleSave();
  };
  ctx.actions.redo = [&]() {
    if (!undoStack.redo(state, session)) return;
    if (undoStack.pendingPresetName) {
      const int idx = presetIndexByName(*undoStack.pendingPresetName);
      const app::TextureEntry* e = idx >= 0 ? app::loadFullPreset(idx, assetDir) : nullptr;
      if (e) {
        state.selectedPreset = idx;
        applyTextureEntry(*e, /*applyDefaults=*/false);
      }
      undoStack.pendingPresetName.reset();
    }
    applyPreview();
    autosave.scheduleSave();
  };

  ctx.actions.smartResolution = [&]() {
    const app::TextureEntry* e = activeTextureEntry();
    if (!e || session.geometry().positions.empty()) return;
    core::DisplacementSettings ds = app::toDisplacementSettings(state.settings);
    core::SmartResolutionResult r =
        core::computeSmartResolution(session.geometry(), session.bounds(), ds, e->image);
    if (!r.ok) return;
    // Matched pair, applied in lockstep (main.js: applySmartResolution).
    state.settings.refineLength = r.edge;
    state.settings.maxTriangles = (int)r.diagnostics.recommendedMaxTri;
    state.meshDirty = true;
    applyPreview();
  };

  // "{meshName}_{texLabel}_amp{amplitude}.{ext}" — same scheme as
  // handleExport's baseName, used as the save dialog's suggested filename.
  auto exportFileName = [&](const char* ext) {
    const app::TextureEntry* e = activeTextureEntry();
    std::string texLabel = e ? e->name : "texture";
    for (char& c : texLabel)
      if (c == ' ') c = '-';
    char amp[16];
    std::snprintf(amp, sizeof(amp), "%.2f", state.settings.amplitude);
    std::string ampLabel = amp;
    for (char& c : ampLabel)
      if (c == '.') c = 'p';
    return state.meshName + "_" + texLabel + "_amp" + ampLabel + "." + ext;
  };

  ctx.actions.exportStl = [&]() {
    const app::TextureEntry* e = activeTextureEntry();
    if (!e || pipelineRunner.running()) return;
    auto path = app::showSaveFileDialog("Export STL", "STL files", "*.stl",
                                        exportFileName("stl").c_str(), "stl");
    if (!path) return;
    pipelineRunner.start(app::PipelineJob::ExportStl, state, session, e->image, *path);
  };
  ctx.actions.export3mf = [&]() {
    const app::TextureEntry* e = activeTextureEntry();
    if (!e || pipelineRunner.running()) return;
    auto path = app::showSaveFileDialog("Export 3MF", "3MF files", "*.3mf",
                                        exportFileName("3mf").c_str(), "3mf");
    if (!path) return;
    pipelineRunner.start(app::PipelineJob::Export3mf, state, session, e->image, *path);
  };
  ctx.actions.bake = [&]() {
    const app::TextureEntry* e = activeTextureEntry();
    if (!e || pipelineRunner.running()) return;
    pipelineRunner.start(app::PipelineJob::Bake, state, session, e->image, "");
  };

  // .texturify project save/load. Simplifications vs. main.js (no dialogs in
  // this native port — see CLAUDE.md): export always bundles whatever is
  // available (model + mask if a model is loaded, texture.png if a custom
  // texture is active) rather than asking via checkboxes; import always
  // replaces the model when the file has one (our app always has SOME
  // geometry loaded — the default cube stands in for main.js's "no model
  // yet" — so main.js's "keep current model" default doesn't translate
  // directly); poseRotation replay on import is not yet wired (loads in the
  // file's stored pose, un-rotated).
  ctx.actions.newProject = [&]() {
    session.setGeometry(makeDefaultCube(), "cube_50x50x50");
    app::SettingsSnapshot defaults;
    app::applySettingsSnapshot(state, defaults);
    session.setSelectionModeImmediate(false);
    session.clearExclusions();
    const int defaultIdx = presetIndexByName("Crystal");
    if (defaultIdx >= 0) {
      const app::TextureEntry* e = app::loadFullPreset(defaultIdx, assetDir);
      if (e) {
        state.selectedPreset = defaultIdx;
        applyTextureEntry(*e, /*applyDefaults=*/true);
      }
    }
    state.meshDirty = false;
    applyPreview();
    undoStack.reset(state, session);
    autosave.scheduleSave();
  };

  // Shared by ctx.actions.saveProject and --test-project.
  auto doSaveProjectTo = [&](const std::string& path) {
    app::SettingsSnapshot snap = app::captureSettingsSnapshot(state);
    const bool hasModel = !session.geometry().positions.empty();
    std::vector<uint8_t> modelBytes, maskBytes, textureBytes;

    if (hasModel) {
      core::Geometry restored = session.geometry();
      app::restoreOriginalPose(restored.positions,
                               restored.normals.empty() ? nullptr : &restored.normals,
                               session.poseRot(), session.poseTrans());
      modelBytes = core::exportSTL(restored);

      std::vector<int32_t> excluded = session.collectMaskFaces();
      const bool selMode = state.brushMode == app::BrushMode::Include;
      if (!excluded.empty() || selMode) {
        app::JsonValue maskJson = app::JsonValue::object();
        maskJson.set("selectionMode", selMode);
        app::JsonArray arr;
        for (int32_t f : excluded) arr.push_back(f);
        maskJson.set("excluded", app::JsonValue(std::move(arr)));
        const std::string s = maskJson.dump();
        maskBytes.assign(s.begin(), s.end());
      }
    }

    if (customTextureEntry) {
      snap.activeMapName = customTextureEntry->name;
      snap.activeMapIsCustom = true;
      const core::ImageDataRGBA& img = customTextureEntry->image;
      int len = 0;
      unsigned char* png =
          stbi_write_png_to_mem(img.data.data(), img.width * 4, img.width, img.height, 4, &len);
      if (png) {
        textureBytes.assign(png, png + len);
        std::free(png);
      }
    }

    std::vector<uint8_t> zip = app::buildProjectFile(
        snap, hasModel ? &modelBytes : nullptr, maskBytes.empty() ? nullptr : &maskBytes,
        textureBytes.empty() ? nullptr : &textureBytes, session.poseRot());
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f) {
      std::fwrite(zip.data(), 1, zip.size(), f);
      std::fclose(f);
    }
  };

  ctx.actions.saveProject = [&]() {
    auto path = app::showSaveFileDialog("Save Project", "Texturify project files",
                                        "*.texturify", (state.meshName + ".texturify").c_str(),
                                        "texturify");
    if (path) doSaveProjectTo(*path);
  };

  // Shared by ctx.actions.openProject and --test-project.
  auto doOpenProjectFrom = [&](const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return;
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> bytes(sz > 0 ? (size_t)sz : 0);
    if (sz > 0) std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);

    app::ParsedProjectFile parsed = app::parseProjectFile(path, bytes.data(), bytes.size());
    if (!parsed.ok) return;

    if (parsed.isBareModel) {
      core::LoadResult result = core::loadModelBytes(path, bytes.data(), bytes.size());
      if (!result.ok) return;
      session.setGeometry(std::move(result.geometry), fileBaseName(path, /*stripExt=*/true));
      state.meshDirty = true;
      applyPreview();
      undoStack.reset(state, session);
      autosave.scheduleSave();
      return;
    }

    const bool loadModel = parsed.hasModel;
    if (loadModel) {
      core::LoadResult modelResult = core::loadModelBytes(
          "model.stl", parsed.modelStlBytes.data(), parsed.modelStlBytes.size());
      if (modelResult.ok)
        session.setGeometry(std::move(modelResult.geometry), fileBaseName(path, /*stripExt=*/true));
    }

    if (parsed.hasSettings) app::applySettingsSnapshot(state, parsed.settings);

    if (loadModel && parsed.hasMask) {
      session.setSelectionModeImmediate(parsed.maskSelectionMode);
      session.seedExcludedFaces(parsed.maskExcluded);
    }

    if (parsed.hasTexture) {
      // Decode via a temp file so the existing fit/resize pipeline (512px
      // cap, canvas-style resize) stays exactly what loadCustomTexture uses.
      const std::string tmpFile =
          (std::filesystem::temp_directory_path() /
           ("texturify_import_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png"))
              .string();
      FILE* tf = std::fopen(tmpFile.c_str(), "wb");
      if (tf) {
        std::fwrite(parsed.texturePngBytes.data(), 1, parsed.texturePngBytes.size(), tf);
        std::fclose(tf);
        auto entry = app::loadCustomTexture(tmpFile);
        std::remove(tmpFile.c_str());
        if (entry) {
          customTextureEntry = std::move(entry);
          customTextureEntry->name = (parsed.hasSettings && !parsed.settings.activeMapName.empty())
                                        ? parsed.settings.activeMapName
                                        : "imported-texture.png";
          if (customThumbTex) glDeleteTextures(1, &customThumbTex);
          core::ImageDataRGBA thumb = app::makeThumbnail(customTextureEntry->image);
          customThumbTex = render::createTextureRGBA(thumb.data.data(), thumb.width, thumb.height,
                                                     /*repeat=*/false, /*linear=*/true,
                                                     /*mipmaps=*/true, /*flipY=*/false);
          ctx.customThumb = (ImTextureID)(intptr_t)customThumbTex;
          state.selectedPreset = -1;
          state.customTextureName = customTextureEntry->name;
          applyTextureEntry(*customTextureEntry, /*applyDefaults=*/false);
        }
      }
    } else if (parsed.hasSettings && !parsed.settings.activeMapName.empty() &&
              !parsed.settings.activeMapIsCustom) {
      const int idx = presetIndexByName(parsed.settings.activeMapName);
      const app::TextureEntry* e = idx >= 0 ? app::loadFullPreset(idx, assetDir) : nullptr;
      if (e) {
        state.selectedPreset = idx;
        applyTextureEntry(*e, /*applyDefaults=*/false);
      }
    }

    state.meshDirty = true;
    applyPreview();
    if (loadModel) undoStack.reset(state, session);
    else undoStack.flush(state, session); // settings-only counts as one undoable edit
    autosave.scheduleSave();
  };

  ctx.actions.openProject = [&]() {
    auto path = app::showOpenFileDialog("Open Project or Model", "Texturify/model/project files",
                                        "*.texturify;*.stl;*.obj;*.3mf");
    if (path) doOpenProjectFrom(*path);
  };

  // Restore last session's settings (main.js's _restoreSessionSettings()),
  // before any --preset/--mapping debug overrides so those still win.
  if (auto snap = app::SessionAutosave::load()) {
    app::applySettingsSnapshot(state, *snap);
    if (!snap->activeMapName.empty() && !snap->activeMapIsCustom) {
      const int idx = presetIndexByName(snap->activeMapName);
      const app::TextureEntry* e = idx >= 0 ? app::loadFullPreset(idx, assetDir) : nullptr;
      if (e) {
        state.selectedPreset = idx;
        applyTextureEntry(*e, /*applyDefaults=*/false);
      }
    }
  }

  if (startPreset >= 0 && startPreset < app::kPresetTextureCount)
    ctx.actions.selectPreset(startPreset);
  if (startMapping >= 0) state.settings.mappingMode = startMapping;
  if (startAmplitude >= 0) state.settings.amplitude = startAmplitude;
  if (testDispPreview) {
    state.displacementPreview3D = true;
    session.setDisplacementPreview(true);
  }
  if (startWireframe) {
    state.wireframe = true;
    viewer.setWireframe(true);
  }

  if (testLoadPath) {
    core::LoadResult result = core::loadModelFile(testLoadPath);
    if (!result.ok) {
      std::fprintf(stderr, "--test-load: failed to load %s\n", testLoadPath);
      return 1;
    }
    session.setGeometry(std::move(result.geometry),
                        fileBaseName(testLoadPath, /*stripExt=*/true));
    state.meshDirty = true;
  }

  applyPreview();
  rebuildCylinderSilhouette(); // initial build for the startup cube
  undoStack.reset(state, session); // initial baseline (main.js: _baselineSnapshot = _captureUndoSnapshot())

  // showWelcomeIfNeeded(): auto-popup once per release, unless the user
  // already dismissed this exact version with "don't show again" checked.
  // Suppressed for --screenshot/--test-* runs so those keep verifying the
  // real UI underneath rather than a modal.
  const bool isAutomatedRun =
      screenshotPath || testExportPath || testBake || testUndo || testProjectPath;
  if (!isAutomatedRun && nativePrefs.welcomeSeenVersion != kWelcomeLastUpdated) {
    state.welcomeOpen = true;
    state.welcomeAllowDismissPersist = true;
  }
  if (testModal) {
    if (std::strcmp(testModal, "welcome") == 0) state.welcomeOpen = true;
    else if (std::strcmp(testModal, "license") == 0) state.licenseOpen = true;
    else if (std::strcmp(testModal, "imprint") == 0) state.imprintOpen = true;
  }

  if (testExportPath || testBake) {
    const app::TextureEntry* e = activeTextureEntry();
    if (!e) {
      std::fprintf(stderr, "--test-export/--test-bake: no texture selected (use --preset)\n");
      return 1;
    }
    const bool is3mf = testExportPath && fileBaseName(testExportPath, false).size() >= 4 &&
                      fileBaseName(testExportPath, false).substr(
                          fileBaseName(testExportPath, false).size() - 4) == ".3mf";
    bool ok = testBake
        ? pipelineRunner.start(app::PipelineJob::Bake, state, session, e->image, "")
        : pipelineRunner.start(is3mf ? app::PipelineJob::Export3mf : app::PipelineJob::ExportStl,
                              state, session, e->image, testExportPath);
    if (!ok) {
      std::fprintf(stderr, "--test-export/--test-bake: failed to start\n");
      return 1;
    }
  }

  if (testProjectPath) {
    auto fail = [](const char* msg) { std::fprintf(stderr, "test-project: FAIL: %s\n", msg); };
    bool ok = true;

    state.settings.amplitude = 0.33;
    state.settings.rotation = 12.0;
    const size_t triBefore = session.triangleCount();

    doSaveProjectTo(testProjectPath);

    state.settings.amplitude = 0.99; // clobber so the reload is observable
    state.settings.rotation = 0.0;

    doOpenProjectFrom(testProjectPath);

    if (state.settings.amplitude != 0.33) { fail("amplitude did not round-trip"); ok = false; }
    if (state.settings.rotation != 12.0) { fail("rotation did not round-trip"); ok = false; }
    if (session.triangleCount() != triBefore) { fail("triangle count changed across round-trip"); ok = false; }

    std::fprintf(stdout, "test-project: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
  }

  if (testUndo) {
    auto fail = [](const char* msg) { std::fprintf(stderr, "test-undo: FAIL: %s\n", msg); };
    bool ok = true;

    // JSON round-trip.
    app::SettingsSnapshot snap;
    snap.textureHeight = 0.42;
    snap.mappingMode = 3;
    snap.cylinderCenterX = 1.5;
    snap.activeMapName = "Crystal";
    app::JsonValue j = app::toJson(snap);
    auto parsed = app::parseJson(j.dump());
    if (!parsed) { fail("json parse failed"); ok = false; }
    else {
      app::SettingsSnapshot back = app::fromJson(*parsed);
      if (!back.settingsEqual(snap)) { fail("json round-trip mismatch"); ok = false; }
      if (!back.cylinderCenterX || *back.cylinderCenterX != 1.5) { fail("cylinderCenterX round-trip"); ok = false; }
    }

    // Undo/redo over settingsChanged.
    state.settings.amplitude = 0.10;
    ctx.actions.settingsChanged();
    undoStack.flush(state, session);
    state.settings.amplitude = 0.20;
    ctx.actions.settingsChanged();
    undoStack.flush(state, session);
    if (!undoStack.canUndo()) { fail("expected canUndo after two distinct edits"); ok = false; }
    ctx.actions.undo();
    if (state.settings.amplitude != 0.10) { fail("undo did not restore amplitude=0.10"); ok = false; }
    if (!undoStack.canRedo()) { fail("expected canRedo after undo"); ok = false; }
    ctx.actions.redo();
    if (state.settings.amplitude != 0.20) { fail("redo did not restore amplitude=0.20"); ok = false; }

    // Mask undo/redo.
    session.clearExclusions();
    undoStack.flush(state, session);
    session.onPointerDown(0, 0, 0); // likely a miss on the default cube's screen-space; ignore result
    std::vector<int32_t> before = session.collectMaskFaces();
    session.seedExcludedFaces({0, 1});
    undoStack.scheduleCapture();
    autosave.scheduleSave();
    undoStack.flush(state, session);
    ctx.actions.undo();
    std::vector<int32_t> afterUndo = session.collectMaskFaces();
    if (afterUndo.size() != before.size()) { fail("mask undo did not restore empty mask"); ok = false; }

    // Session autosave round-trip (writes to the real %APPDATA% path).
    state.settings.rotation = 77.5;
    autosave.flush(state);
    auto loaded = app::SessionAutosave::load();
    if (!loaded) { fail("session autosave load failed"); ok = false; }
    else if (loaded->rotation != 77.5) { fail("session autosave rotation mismatch"); ok = false; }

    std::fprintf(stdout, "test-undo: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
  }

  bool prevBtn[3] = {false, false, false};
  double prevVpX = -1e30, prevVpY = -1e30;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    int fbW = 1, fbH = 1, winW = 1, winH = 1;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    glfwGetWindowSize(window, &winW, &winH);
    if (fbW <= 0 || fbH <= 0) continue; // minimized
    double sx = winW > 0 ? (double)fbW / winW : 1;
    double sy = winH > 0 ? (double)fbH / winH : 1;

    // Layout in ImGui (window) coordinates
    ctx.layout = ui::computeLayout((float)winW, (float)winH, theme.scale);
    const ImVec2 vpMin = ctx.layout.vpMin, vpMax = ctx.layout.vpMax;
    int vpX = (int)(vpMin.x * sx), vpY = (int)(vpMin.y * sy);
    int vpW = std::max(1, (int)((vpMax.x - vpMin.x) * sx));
    int vpH = std::max(1, (int)((vpMax.y - vpMin.y) * sy));

    // ── Pointer routing (viewport-relative framebuffer px) ────────────────
    // io.WantCaptureMouse is last frame's value — exactly what we want, since
    // this frame's UI hasn't been declared yet.
    ImGuiIO& io = ImGui::GetIO();
    bool shiftDown = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                     glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    bool ctrlDown = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                    glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    session.update(shiftDown, ctrlDown);
    pipelineRunner.poll(state, session);
    if (pipelineRunner.consumeBakeCompleted()) undoStack.reset(state, session);
    undoStack.update(state, session);
    autosave.update(state);

    // ── Advanced mesh diagnostics (background thread) ─────────────────────
    {
      const int curEpoch = session.geometryEpoch();
      if (curEpoch != diagEpochSeen) {
        diagEpochSeen = curEpoch;
        diagRunner.cancel(); // mesh changed mid-run; discard when it finishes
      }
      core::ExpensiveDiagnostics diagResult;
      if (diagRunner.poll(curEpoch, diagResult)) {
        session.setAdvancedDiag(diagResult);
      } else if (state.diagAdvancedRunning && !diagRunner.running()) {
        state.diagAdvancedRunning = false; // finished but discarded as stale
      }
    }

    if ((testExportPath || testBake) && !state.pipelineRunning &&
        !pipelineRunner.running()) {
      if (!state.pipelineErrorMessage.empty()) {
        std::fprintf(stderr, "%s: %s\n", testBake ? "bake" : "export",
                    state.pipelineErrorMessage.c_str());
        glfwSetWindowShouldClose(window, 1);
      } else {
        std::fprintf(stdout, "%s: OK (%zu tris%s)\n", testBake ? "bake" : "export",
                    session.triangleCount(),
                    state.triLimitWarning ? ", safety cap hit" : "");
        glfwSetWindowShouldClose(window, 1);
      }
    }

    // Lazy rebuild: only when cylindrical mode is showing the panel AND the
    // base mesh topology changed since the last build (mirrors main.js's
    // `_cylSilhouetteGeometry === currentGeometry` identity check, done only
    // while the panel is actually visible).
    if (state.settings.mappingMode == 3 &&
        ctx.cylinderSilhouette.geometryEpoch != session.geometryEpoch())
      rebuildCylinderSilhouette();

    static bool escWasDown = false;
    bool escDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    if (escDown && !escWasDown && !io.WantTextInput) {
      if (state.rotateGizmo) { state.rotateGizmo = false; session.setRotateMode(false); }
      if (state.placeOnFaceActive) session.setPlaceOnFace(false);
      state.paintingEnabled = false;
    }
    escWasDown = escDown;

    // Ctrl/Cmd+Z = undo, Ctrl+Shift+Z or Ctrl+Y = redo — skipped while a text
    // field has focus, matching main.js's tagName/type guard.
    static bool zWasDown = false, yWasDown = false;
    bool zDown = glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS;
    bool yDown = glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS;
    if (ctrlDown && !io.WantTextInput) {
      if (zDown && !zWasDown) {
        if (shiftDown) ctx.actions.redo(); else ctx.actions.undo();
      }
      if (yDown && !yWasDown) ctx.actions.redo();
    }
    zWasDown = zDown;
    yWasDown = yDown;

    double cx, cy;
    glfwGetCursorPos(window, &cx, &cy);
    double mx = (cx - vpMin.x) * sx, my = (cy - vpMin.y) * sy;
    bool inViewport = cx >= vpMin.x && cx < vpMax.x && cy >= vpMin.y &&
                      cy < vpMax.y;
    if (mx != prevVpX || my != prevVpY) {
      // Painting/hover are canvas-scoped in the JS (mousemove only fires
      // over the element); orbit/pan drags continue outside the viewport.
      if (inViewport) session.onPointerMove(mx, my);
      viewer.onPointerMove(mx, my);
      prevVpX = mx;
      prevVpY = my;
    }
    for (int b = 0; b < 3; b++) {
      bool down = glfwGetMouseButton(window, b) == GLFW_PRESS;
      if (down && !prevBtn[b] && inViewport && !io.WantCaptureMouse) {
        bool sessionConsumed =
            b == 0 && (state.paintingEnabled || state.placeOnFaceActive) &&
            session.onPointerDown(mx, my, b);
        if (!sessionConsumed) viewer.onPointerDown(mx, my, b);
      } else if (!down && prevBtn[b]) {
        session.onPointerUp(b);
        viewer.onPointerUp(b);
        if (b == 0) { undoStack.scheduleCapture(); autosave.scheduleSave(); } // catches paint-stroke ends
      }
      prevBtn[b] = down;
    }
    if (g_scrollY != 0) {
      if (inViewport && !io.WantCaptureMouse)
        viewer.onScroll(mx, my, -g_scrollY); // browser deltaY sign
      g_scrollY = 0;
    }

    // ── 3D scene → offscreen viewport target, then glass composite ────────
    glass.resize(fbW, fbH);
    viewer.resize(vpW, vpH);
    glass.beginViewport(vpX, vpY, vpW, vpH);
    viewer.render();
    glass.endViewport();
    glass.composite();

    // ── UI frame ───────────────────────────────────────────────────────────
    ctx.canUndo = undoStack.canUndo();
    ctx.canRedo = undoStack.canRedo();
    ctx.windowMaximized = chrome.isMaximized();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ui::drawUi(ctx);
    ImGui::Render();
    // Feed this frame's toolbar layout back to the custom window chrome so
    // the next WM_NCHITTEST knows which part of the toolbar is a real button
    // (see app::CustomChrome / ui/toolbar.cpp).
    chrome.updateHitTestRegion(ctx.layout.toolbarMax.y - ctx.layout.toolbarMin.y,
                               ctx.dragExemptRects);

    glViewport(0, 0, fbW, fbH);
    glClearColor(0x0a / 255.0f, 0x0a / 255.0f, 0x0e / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    static int frameCount = 0;
    if (screenshotPath && ++frameCount == 6) {
      std::vector<uint8_t> px((size_t)fbW * fbH * 4);
      glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
      stbi_flip_vertically_on_write(1);
      stbi_write_png(screenshotPath, fbW, fbH, 4, px.data(), fbW * 4);
      glfwSetWindowShouldClose(window, 1);
    }
    glfwSwapBuffers(window);
  }

  autosave.flush(state); // final save on exit, regardless of the debounce window

  if (displacementTex) glDeleteTextures(1, &displacementTex);
  for (int i = 0; i < app::kPresetTextureCount; i++) {
    GLuint t = (GLuint)(intptr_t)ctx.presetThumbs[i];
    if (t) glDeleteTextures(1, &t);
  }
  viewer.destroy();
  glass.destroy();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
