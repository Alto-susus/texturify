// Shared check/IO helpers for the texturify_verify test files.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

inline int g_failures = 0;

inline void check(bool cond, const std::string& what) {
  if (cond) {
    std::printf("  PASS  %s\n", what.c_str());
  } else {
    std::printf("  FAIL  %s\n", what.c_str());
    g_failures++;
  }
}

inline std::vector<uint8_t> readFile(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return {};
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> data((size_t)size);
  if (size > 0 && std::fread(data.data(), 1, (size_t)size, f) != (size_t)size)
    data.clear();
  std::fclose(f);
  return data;
}

inline bool writeFile(const std::string& path,
                      const std::vector<uint8_t>& data) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  size_t w = data.empty() ? 0 : std::fwrite(data.data(), 1, data.size(), f);
  std::fclose(f);
  return w == data.size();
}

template <typename T>
std::vector<T> readArray(const std::string& path) {
  auto bytes = readFile(path);
  std::vector<T> out(bytes.size() / sizeof(T));
  if (!out.empty()) std::memcpy(out.data(), bytes.data(), out.size() * sizeof(T));
  return out;
}

// Minimal value extraction from the golden meta JSON (flat, unique keys).
inline double jsonNum(const std::string& text, const std::string& key,
                      double fallback = 0) {
  size_t p = text.find("\"" + key + "\":");
  if (p == std::string::npos) return fallback;
  p += key.size() + 3;
  return std::strtod(text.c_str() + p, nullptr);
}

inline bool jsonBool(const std::string& text, const std::string& key,
                     bool fallback = false) {
  size_t p = text.find("\"" + key + "\":");
  if (p == std::string::npos) return fallback;
  p += key.size() + 3;
  while (p < text.size() && (text[p] == ' ' || text[p] == '\n')) p++;
  return text.compare(p, 4, "true") == 0;
}

inline std::string readText(const std::string& path) {
  auto bytes = readFile(path);
  return std::string(bytes.begin(), bytes.end());
}

// Golden tests over tools/golden/out fixtures (verify_pipeline.cpp).
void runPipelineGoldenTests(const std::string& filter);
