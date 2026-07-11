#include "app/preset_textures.h"

#include <array>

namespace app {

const TexturePresetDef kTexturePresets[kPresetTextureCount] = {
    {"Basket", "basket.png", 0.5},
    {"Brick", "brick.png", 0.5},
    {"Bubble", "bubble.png", 0.5},
    {"Carbon Fiber", "carbonFiber.jpg", 0.5},
    {"Crystal", "crystal.png", 0.5},
    {"Dots", "dots.png", 0.1},
    {"Grid", "grid.png", 1.0},
    {"Grip Surface", "gripSurface.jpg", 0.5},
    {"Hexagon", "hexagon.jpg", 0.5},
    {"Hexagons", "hexagons.jpg", 1.0},
    {"Isogrid", "isogrid.png", 0.5},
    {"Knitting", "knitting.png", 0.25},
    {"Knurling", "knurling.jpg", 0.15},
    {"Leather 2", "leather2.png", 0.5},
    {"Noise", "noise.jpg", 0.3},
    {"Stripes 1", "stripes.png", 0.5},
    {"Stripes 2", "stripes_02.png", 1.0},
    {"Voronoi", "voronoi.jpg", 0.5},
    {"Weave 1", "weave.png", 0.5},
    {"Weave 2", "weave_02.jpg", 0.5},
    {"Weave 3", "weave_03.jpg", 0.5},
    {"Wood 1", "wood.jpg", 0.5},
    {"Wood 2", "woodgrain_02.jpg", 1.0},
    {"Wood 3", "woodgrain_03.jpg", 1.0},
};

namespace {
// _fullPresetCache equivalent — presets are immutable, so a static cache of
// optionals keyed by index is enough.
std::array<std::optional<TextureEntry>, kPresetTextureCount> g_cache;
} // namespace

const TextureEntry* loadFullPreset(int idx, const std::string& assetDir) {
  if (idx < 0 || idx >= kPresetTextureCount) return nullptr;
  if (g_cache[idx]) return &*g_cache[idx];
  const TexturePresetDef& p = kTexturePresets[idx];
  core::ImageDataRGBA img =
      core::loadTextureImageRGBA(assetDir + "/textures/" + p.file);
  if (img.data.empty()) return nullptr;
  g_cache[idx] = TextureEntry{p.name, std::move(img), p.defaultScale};
  return &*g_cache[idx];
}

std::optional<TextureEntry> loadCustomTexture(const std::string& filePath) {
  core::ImageDataRGBA img = core::loadTextureImageRGBA(filePath);
  if (img.data.empty()) return std::nullopt;
  // File.name — the path's final component
  size_t slash = filePath.find_last_of("/\\");
  std::string name =
      slash == std::string::npos ? filePath : filePath.substr(slash + 1);
  return TextureEntry{std::move(name), std::move(img), 1.0};
}

core::ImageDataRGBA makeThumbnail(const core::ImageDataRGBA& image) {
  if (image.data.empty()) return {};
  return core::resizeImageRGBA(image, kThumbSize, kThumbSize);
}

} // namespace app
