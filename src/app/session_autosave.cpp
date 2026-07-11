#include "app/session_autosave.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#include "app/app_paths.h"

namespace app {

namespace {
constexpr int kProjectVersion = 1;
}

std::string SessionAutosave::filePath() { return appDataDir() + "/session.json"; }

void SessionAutosave::scheduleSave() {
  _pendingAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
}

void SessionAutosave::update(const AppState& state) {
  if (!_pendingAt || std::chrono::steady_clock::now() < *_pendingAt) return;
  _pendingAt.reset();
  writeNow(state);
}

void SessionAutosave::flush(const AppState& state) {
  _pendingAt.reset();
  writeNow(state);
}

void SessionAutosave::writeNow(const AppState& state) {
  SettingsSnapshot snap = captureSettingsSnapshot(state);
  JsonValue root = toJson(snap);
  root.set("version", kProjectVersion);
  std::ofstream f(filePath(), std::ios::binary | std::ios::trunc);
  if (!f) return; // best-effort — matches main.js's try/catch-and-ignore
  const std::string text = root.dump();
  f.write(text.data(), (std::streamsize)text.size());
}

std::optional<SettingsSnapshot> SessionAutosave::load() {
  std::ifstream f(filePath(), std::ios::binary);
  if (!f) return std::nullopt;
  std::ostringstream ss;
  ss << f.rdbuf();
  auto parsed = parseJson(ss.str());
  if (!parsed || !parsed->isObject()) return std::nullopt;
  return fromJson(*parsed);
}

} // namespace app
