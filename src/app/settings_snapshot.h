// Port of main.js's PERSISTED_KEYS / getSettingsSnapshot / applySettingsSnapshot
// — the flat subset of Settings shared by undo/redo, .texturify project files,
// and session autosave. Deliberately excludes the Advanced/Beta "Regularize
// Mesh" fields and `useDisplacement` — main.js doesn't persist those either.
#pragma once

#include <optional>
#include <string>

#include "app/app_state.h"
#include "app/json.h"
#include "app/preset_textures.h"

namespace app {

struct SettingsSnapshot {
  int mappingMode = 5;
  double scaleU = 0.5, scaleV = 0.5;
  bool lockScale = true;
  double offsetU = 0, offsetV = 0, rotation = 0;
  double textureHeight = 0.5; // -> settings.amplitude on restore (see pipeline_runner note)
  bool invertDisplacement = false;
  bool symmetricDisplacement = false;
  bool noDownwardZ = false;
  bool smoothBottom = true;
  bool harvestFlatFaces = true;
  double harvestTol = 0.005;
  double textureSmoothing = 0;
  double mappingBlend = 1, seamBandWidth = 0.5, capAngle = 20, boundaryFalloff = 0;
  double bottomAngleLimit = 5, topAngleLimit = 0;
  double refineLength = 1.0;
  int maxTriangles = 750'000;
  bool snapSeamlessWrap = true;
  std::optional<double> cylinderCenterX, cylinderCenterY, cylinderRadius;
  bool cylinderPanelMinimized = false;

  // Not part of PERSISTED_KEYS proper, but travels alongside it everywhere
  // main.js uses a settings snapshot (getSettingsSnapshot() adds it in).
  std::string activeMapName;   // preset or custom-texture name; empty = none
  bool activeMapIsCustom = false;

  // PERSISTED_KEYS-only equality (main.js's _undoSnapshotsEqual, minus mask).
  bool settingsEqual(const SettingsSnapshot& o) const;
};

SettingsSnapshot captureSettingsSnapshot(const AppState& state);
// Applies the PERSISTED_KEYS fields onto state.settings; does NOT touch
// texture selection (activeMapName) or the exclusion mask — callers handle
// those (they need session/texture-cache access this pure function doesn't
// have).
void applySettingsSnapshot(AppState& state, const SettingsSnapshot& snap);

JsonValue toJson(const SettingsSnapshot& snap);
// Missing keys fall back to `fallback`'s values (matches JS's `!= null` guards
// in applySettingsSnapshot, which leave untouched settings alone).
SettingsSnapshot fromJson(const JsonValue& obj, const SettingsSnapshot& fallback = {});

} // namespace app
