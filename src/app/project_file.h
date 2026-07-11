// Port of main.js's .texturify project save/load (lines ~5278-5622): a ZIP
// containing settings.json (required), model.stl + mask.json (optional, the
// model written in its ORIGINAL pre-rotation pose so poseRotation replay
// keeps round-trips stable), and texture.png (optional custom displacement
// map). Uses miniz (already vendored for the 3MF exporter).
//
// Simplification vs. main.js: the original pops a dialog to ask "include
// model?"/"include texture?" before export and "replace model or settings
// only?" before import. This port always includes whatever is available on
// export, and on import defaults to main.js's own fallback heuristic
// (settings-only if a model is already loaded, full replace otherwise) with
// no prompt — see CLAUDE.md.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "app/settings_snapshot.h"
#include "render/math3d.h"

namespace app {

std::vector<uint8_t> buildProjectFile(const SettingsSnapshot& settings,
                                      const std::vector<uint8_t>* modelStlBytes,
                                      const std::vector<uint8_t>* maskJsonBytes,
                                      const std::vector<uint8_t>* texturePngBytes,
                                      const render::Quat& poseRot);

struct ParsedProjectFile {
  bool ok = false;
  std::string error;
  // Not a .texturify zip at all (bare .stl/.obj/.3mf, or non-zip data) — the
  // caller should route `data`/`size` to core::loadModelBytes instead
  // (main.js's isModelExt || !isZip fallback).
  bool isBareModel = false;

  bool hasSettings = false;
  SettingsSnapshot settings;

  bool hasModel = false;
  std::vector<uint8_t> modelStlBytes;

  bool hasMask = false;
  bool maskSelectionMode = false;
  std::vector<int32_t> maskExcluded;

  bool hasTexture = false;
  std::vector<uint8_t> texturePngBytes;

  // Only meaningful when hasModel; identity rotations are omitted (matches
  // main.js only writing poseRotation when `Math.abs(currentPoseRot.w) < 1 - 1e-12`).
  std::optional<render::Quat> poseRotation;
};

// `filename` is used for the extension/magic-byte sniff (main.js's
// isModelExt/isZip check) — pass the user-chosen file name, not just bytes.
ParsedProjectFile parseProjectFile(const std::string& filename, const uint8_t* data,
                                   size_t size);

} // namespace app
