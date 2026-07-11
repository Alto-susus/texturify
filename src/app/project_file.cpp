#include "app/project_file.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include <miniz.h>

namespace app {

namespace {

bool hasExt(const std::string& name, const char* ext) {
  const size_t n = std::strlen(ext);
  if (name.size() < n) return false;
  std::string tail = name.substr(name.size() - n);
  std::transform(tail.begin(), tail.end(), tail.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return tail == ext;
}

bool looksLikeZip(const uint8_t* data, size_t size) {
  return size >= 4 && data[0] == 0x50 && data[1] == 0x4B && data[2] == 0x03 && data[3] == 0x04;
}

} // namespace

std::vector<uint8_t> buildProjectFile(const SettingsSnapshot& settings,
                                      const std::vector<uint8_t>* modelStlBytes,
                                      const std::vector<uint8_t>* maskJsonBytes,
                                      const std::vector<uint8_t>* texturePngBytes,
                                      const render::Quat& poseRot) {
  JsonValue payload = toJson(settings);
  payload.set("version", 1);
  if (modelStlBytes && std::abs(poseRot.w) < 1 - 1e-12) {
    JsonArray q;
    q.push_back(poseRot.x);
    q.push_back(poseRot.y);
    q.push_back(poseRot.z);
    q.push_back(poseRot.w);
    payload.set("poseRotation", JsonValue(std::move(q)));
  }
  const std::string settingsJson = payload.dump();

  mz_zip_archive zip;
  std::memset(&zip, 0, sizeof(zip));
  mz_zip_writer_init_heap(&zip, 0,
                          settingsJson.size() + (modelStlBytes ? modelStlBytes->size() : 0) + 4096);
  mz_zip_writer_add_mem(&zip, "settings.json", settingsJson.data(), settingsJson.size(), 6);
  if (modelStlBytes)
    mz_zip_writer_add_mem(&zip, "model.stl", modelStlBytes->data(), modelStlBytes->size(), 6);
  if (maskJsonBytes)
    mz_zip_writer_add_mem(&zip, "mask.json", maskJsonBytes->data(), maskJsonBytes->size(), 6);
  if (texturePngBytes)
    mz_zip_writer_add_mem(&zip, "texture.png", texturePngBytes->data(), texturePngBytes->size(), 6);

  void* pBuf = nullptr;
  size_t outSize = 0;
  mz_zip_writer_finalize_heap_archive(&zip, &pBuf, &outSize);
  std::vector<uint8_t> out((uint8_t*)pBuf, (uint8_t*)pBuf + outSize);
  mz_zip_writer_end(&zip);
  return out;
}

ParsedProjectFile parseProjectFile(const std::string& filename, const uint8_t* data, size_t size) {
  ParsedProjectFile out;
  const bool isModelExt =
      hasExt(filename, ".stl") || hasExt(filename, ".obj") || hasExt(filename, ".3mf");
  if (isModelExt || !looksLikeZip(data, size)) {
    out.ok = true;
    out.isBareModel = true;
    return out;
  }

  mz_zip_archive zip;
  std::memset(&zip, 0, sizeof(zip));
  if (!mz_zip_reader_init_mem(&zip, data, size, 0)) {
    out.error = "invalid zip data";
    return out;
  }

  auto extract = [&](const char* name, std::vector<uint8_t>& dst) -> bool {
    int idx = mz_zip_reader_locate_file(&zip, name, nullptr, 0);
    if (idx < 0) return false;
    size_t sz = 0;
    void* p = mz_zip_reader_extract_to_heap(&zip, (mz_uint)idx, &sz, 0);
    if (!p) return false;
    dst.assign((uint8_t*)p, (uint8_t*)p + sz);
    mz_free(p);
    return true;
  };

  std::vector<uint8_t> settingsBytes;
  std::optional<JsonValue> settingsJson;
  if (extract("settings.json", settingsBytes)) {
    settingsJson = parseJson(std::string(settingsBytes.begin(), settingsBytes.end()));
    if (settingsJson && settingsJson->isObject()) {
      out.hasSettings = true;
      out.settings = fromJson(*settingsJson);
    }
  }

  out.hasModel = extract("model.stl", out.modelStlBytes);

  std::vector<uint8_t> maskBytes;
  if (extract("mask.json", maskBytes)) {
    auto maskJson = parseJson(std::string(maskBytes.begin(), maskBytes.end()));
    if (maskJson && maskJson->isObject()) {
      out.hasMask = true;
      out.maskSelectionMode = maskJson->getBool("selectionMode", false);
      const JsonValue* excl = maskJson->find("excluded");
      if (excl && excl->isArray())
        for (const JsonValue& v : excl->asArray())
          if (v.isNumber()) out.maskExcluded.push_back((int32_t)v.asNumber());
    }
  }

  out.hasTexture = extract("texture.png", out.texturePngBytes);

  if (out.hasModel && settingsJson && settingsJson->isObject()) {
    const JsonValue* pr = settingsJson->find("poseRotation");
    if (pr && pr->isArray() && pr->asArray().size() == 4) {
      const JsonArray& a = pr->asArray();
      render::Quat q{a[0].asNumber(), a[1].asNumber(), a[2].asNumber(), a[3].asNumber()};
      q = q.normalized();
      if (std::abs(q.w) < 1 - 1e-12) out.poseRotation = q;
    }
  }

  mz_zip_reader_end(&zip);
  out.ok = true;
  return out;
}

} // namespace app
