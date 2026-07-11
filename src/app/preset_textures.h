// Port of reference/js/presetTextures.js — the 24 built-in displacement
// textures (name, image file, default UV scale), full-resolution loading with
// the 512px longest-side cap, and 80px thumbnails.
//
// Deviation: the web app ships precomputed .webp thumbnails; stb_image has no
// webp decoder, so thumbnails are generated from the full image with the same
// canvas-style bilinear resize. Thumbnails are display-only — the sampling
// image (fitted ≤512) is byte-identical to the JS path.
#pragma once

#include <optional>
#include <string>

#include "core/image.h"

namespace app {

inline constexpr int kPresetTextureCount = 24;
inline constexpr int kThumbSize = 80; // presetTextures THUMB

struct TexturePresetDef {
  const char* name;
  const char* file; // relative to <assetDir>/textures/
  double defaultScale;
};

// Same order as IMAGE_PRESETS in presetTextures.js.
extern const TexturePresetDef kTexturePresets[kPresetTextureCount];

// A loaded texture (preset or custom): mirror of the JS entry
// { name, imageData, width, height, defaultScale }.
struct TextureEntry {
  std::string name;
  core::ImageDataRGBA image; // decode → fitDimensions(512) → resize
  double defaultScale = 1;
};

// loadFullPreset(idx): decode + fit the preset image. Results are cached per
// index like the JS _fullPresetCache; returns nullptr on load failure.
const TextureEntry* loadFullPreset(int idx, const std::string& assetDir);

// loadCustomTexture(file): user-supplied image via the same fit path.
// name = file name (with extension), like the JS File.name.
std::optional<TextureEntry> loadCustomTexture(const std::string& filePath);

// 80x80 thumbnail (JS: drawImage(img, 0, 0, 80, 80) — aspect is squashed).
core::ImageDataRGBA makeThumbnail(const core::ImageDataRGBA& image);

} // namespace app
