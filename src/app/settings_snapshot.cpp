#include "app/settings_snapshot.h"

namespace app {

bool SettingsSnapshot::settingsEqual(const SettingsSnapshot& o) const {
  return mappingMode == o.mappingMode && scaleU == o.scaleU && scaleV == o.scaleV &&
        lockScale == o.lockScale && offsetU == o.offsetU && offsetV == o.offsetV &&
        rotation == o.rotation && textureHeight == o.textureHeight &&
        invertDisplacement == o.invertDisplacement &&
        symmetricDisplacement == o.symmetricDisplacement && noDownwardZ == o.noDownwardZ &&
        smoothBottom == o.smoothBottom && harvestFlatFaces == o.harvestFlatFaces &&
        harvestTol == o.harvestTol && textureSmoothing == o.textureSmoothing &&
        mappingBlend == o.mappingBlend && seamBandWidth == o.seamBandWidth &&
        capAngle == o.capAngle && boundaryFalloff == o.boundaryFalloff &&
        bottomAngleLimit == o.bottomAngleLimit && topAngleLimit == o.topAngleLimit &&
        refineLength == o.refineLength && maxTriangles == o.maxTriangles &&
        snapSeamlessWrap == o.snapSeamlessWrap && cylinderCenterX == o.cylinderCenterX &&
        cylinderCenterY == o.cylinderCenterY && cylinderRadius == o.cylinderRadius &&
        cylinderPanelMinimized == o.cylinderPanelMinimized && activeMapName == o.activeMapName;
}

SettingsSnapshot captureSettingsSnapshot(const AppState& state) {
  const Settings& s = state.settings;
  SettingsSnapshot snap;
  snap.mappingMode = s.mappingMode;
  snap.scaleU = s.scaleU;
  snap.scaleV = s.scaleV;
  snap.lockScale = s.lockScale;
  snap.offsetU = s.offsetU;
  snap.offsetV = s.offsetV;
  snap.rotation = s.rotation;
  snap.textureHeight = s.amplitude; // s.amplitude plays textureHeight's role
  snap.invertDisplacement = s.invertDisplacement;
  snap.symmetricDisplacement = s.symmetricDisplacement;
  snap.noDownwardZ = s.noDownwardZ;
  snap.smoothBottom = s.smoothBottom;
  snap.harvestFlatFaces = s.harvestFlatFaces;
  snap.harvestTol = s.harvestTol;
  snap.textureSmoothing = s.textureSmoothing;
  snap.mappingBlend = s.mappingBlend;
  snap.seamBandWidth = s.seamBandWidth;
  snap.capAngle = s.capAngle;
  snap.boundaryFalloff = s.boundaryFalloff;
  snap.bottomAngleLimit = s.bottomAngleLimit;
  snap.topAngleLimit = s.topAngleLimit;
  snap.refineLength = s.refineLength;
  snap.maxTriangles = s.maxTriangles;
  snap.snapSeamlessWrap = s.snapSeamlessWrap;
  snap.cylinderCenterX = s.cylinderCenterX;
  snap.cylinderCenterY = s.cylinderCenterY;
  snap.cylinderRadius = s.cylinderRadius;
  snap.cylinderPanelMinimized = s.cylinderPanelMinimized;
  if (state.selectedPreset >= 0 && state.selectedPreset < kPresetTextureCount) {
    snap.activeMapName = kTexturePresets[state.selectedPreset].name;
    snap.activeMapIsCustom = false;
  } else if (!state.customTextureName.empty()) {
    snap.activeMapName = state.customTextureName;
    snap.activeMapIsCustom = true;
  }
  return snap;
}

void applySettingsSnapshot(AppState& state, const SettingsSnapshot& snap) {
  Settings& s = state.settings;
  s.mappingMode = snap.mappingMode;
  s.scaleU = snap.scaleU;
  s.scaleV = snap.scaleV;
  s.lockScale = snap.lockScale;
  s.offsetU = snap.offsetU;
  s.offsetV = snap.offsetV;
  s.rotation = snap.rotation;
  s.invertDisplacement = snap.invertDisplacement; // before amplitude, matches main.js ordering
  s.amplitude = snap.textureHeight;
  s.symmetricDisplacement = snap.symmetricDisplacement;
  s.noDownwardZ = snap.noDownwardZ;
  s.smoothBottom = snap.smoothBottom;
  s.harvestFlatFaces = snap.harvestFlatFaces;
  s.harvestTol = snap.harvestTol;
  s.textureSmoothing = snap.textureSmoothing;
  s.mappingBlend = snap.mappingBlend;
  s.seamBandWidth = snap.seamBandWidth;
  s.capAngle = snap.capAngle;
  s.boundaryFalloff = snap.boundaryFalloff;
  s.bottomAngleLimit = snap.bottomAngleLimit;
  s.topAngleLimit = snap.topAngleLimit;
  s.refineLength = snap.refineLength;
  s.maxTriangles = snap.maxTriangles;
  s.snapSeamlessWrap = snap.snapSeamlessWrap;
  s.cylinderCenterX = snap.cylinderCenterX;
  s.cylinderCenterY = snap.cylinderCenterY;
  s.cylinderRadius = snap.cylinderRadius;
  s.cylinderPanelMinimized = snap.cylinderPanelMinimized;
}

JsonValue toJson(const SettingsSnapshot& snap) {
  JsonValue o = JsonValue::object();
  o.set("mappingMode", snap.mappingMode);
  o.set("scaleU", snap.scaleU);
  o.set("scaleV", snap.scaleV);
  o.set("lockScale", snap.lockScale);
  o.set("offsetU", snap.offsetU);
  o.set("offsetV", snap.offsetV);
  o.set("rotation", snap.rotation);
  o.set("textureHeight", snap.textureHeight);
  o.set("invertDisplacement", snap.invertDisplacement);
  o.set("symmetricDisplacement", snap.symmetricDisplacement);
  o.set("noDownwardZ", snap.noDownwardZ);
  o.set("smoothBottom", snap.smoothBottom);
  o.set("harvestFlatFaces", snap.harvestFlatFaces);
  o.set("harvestTol", snap.harvestTol);
  o.set("textureSmoothing", snap.textureSmoothing);
  o.set("mappingBlend", snap.mappingBlend);
  o.set("seamBandWidth", snap.seamBandWidth);
  o.set("capAngle", snap.capAngle);
  o.set("boundaryFalloff", snap.boundaryFalloff);
  o.set("bottomAngleLimit", snap.bottomAngleLimit);
  o.set("topAngleLimit", snap.topAngleLimit);
  o.set("refineLength", snap.refineLength);
  o.set("maxTriangles", snap.maxTriangles);
  o.set("snapSeamlessWrap", snap.snapSeamlessWrap);
  o.set("cylinderCenterX", snap.cylinderCenterX ? JsonValue(*snap.cylinderCenterX) : JsonValue(nullptr));
  o.set("cylinderCenterY", snap.cylinderCenterY ? JsonValue(*snap.cylinderCenterY) : JsonValue(nullptr));
  o.set("cylinderRadius", snap.cylinderRadius ? JsonValue(*snap.cylinderRadius) : JsonValue(nullptr));
  o.set("cylinderPanelMinimized", snap.cylinderPanelMinimized);
  o.set("activeMapName", snap.activeMapName);
  o.set("activeMapIsCustom", snap.activeMapIsCustom);
  return o;
}

SettingsSnapshot fromJson(const JsonValue& obj, const SettingsSnapshot& fallback) {
  SettingsSnapshot s = fallback;
  if (!obj.isObject()) return s;
  s.mappingMode = (int)obj.getNumber("mappingMode", s.mappingMode);
  s.scaleU = obj.getNumber("scaleU", s.scaleU);
  s.scaleV = obj.getNumber("scaleV", s.scaleV);
  s.lockScale = obj.getBool("lockScale", s.lockScale);
  s.offsetU = obj.getNumber("offsetU", s.offsetU);
  s.offsetV = obj.getNumber("offsetV", s.offsetV);
  s.rotation = obj.getNumber("rotation", s.rotation);
  s.textureHeight = obj.getNumber("textureHeight", s.textureHeight);
  s.invertDisplacement = obj.getBool("invertDisplacement", s.invertDisplacement);
  s.symmetricDisplacement = obj.getBool("symmetricDisplacement", s.symmetricDisplacement);
  s.noDownwardZ = obj.getBool("noDownwardZ", s.noDownwardZ);
  s.smoothBottom = obj.getBool("smoothBottom", s.smoothBottom);
  s.harvestFlatFaces = obj.getBool("harvestFlatFaces", s.harvestFlatFaces);
  s.harvestTol = obj.getNumber("harvestTol", s.harvestTol);
  s.textureSmoothing = obj.getNumber("textureSmoothing", s.textureSmoothing);
  s.mappingBlend = obj.getNumber("mappingBlend", s.mappingBlend);
  s.seamBandWidth = obj.getNumber("seamBandWidth", s.seamBandWidth);
  s.capAngle = obj.getNumber("capAngle", s.capAngle);
  s.boundaryFalloff = obj.getNumber("boundaryFalloff", s.boundaryFalloff);
  s.bottomAngleLimit = obj.getNumber("bottomAngleLimit", s.bottomAngleLimit);
  s.topAngleLimit = obj.getNumber("topAngleLimit", s.topAngleLimit);
  s.refineLength = obj.getNumber("refineLength", s.refineLength);
  s.maxTriangles = (int)obj.getNumber("maxTriangles", s.maxTriangles);
  s.snapSeamlessWrap = obj.getBool("snapSeamlessWrap", s.snapSeamlessWrap);
  if (obj.has("cylinderCenterX")) s.cylinderCenterX = obj.getOptNumber("cylinderCenterX");
  if (obj.has("cylinderCenterY")) s.cylinderCenterY = obj.getOptNumber("cylinderCenterY");
  if (obj.has("cylinderRadius")) s.cylinderRadius = obj.getOptNumber("cylinderRadius");
  s.cylinderPanelMinimized = obj.getBool("cylinderPanelMinimized", s.cylinderPanelMinimized);
  s.activeMapName = obj.getString("activeMapName", s.activeMapName);
  s.activeMapIsCustom = obj.getBool("activeMapIsCustom", s.activeMapIsCustom);
  return s;
}

} // namespace app
