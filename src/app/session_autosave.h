// Port of main.js's sessionStorage-based settings autosave (_autoSaveSettings/
// _restoreSessionSettings). Native apps have no sessionStorage equivalent, so
// this persists to <%APPDATA%>/Texturify/session.json instead — unlike
// sessionStorage this survives an app restart, a deliberate, reasonable
// improvement over the browser-tab-scoped original (there is no "closing the
// tab" concept to reset to defaults from).
#pragma once

#include <chrono>
#include <optional>

#include "app/settings_snapshot.h"

namespace app {

class SessionAutosave {
public:
  // ~300ms debounce, restarting the window on every call (main.js's
  // setTimeout(..., 300) inside _autoSaveSettings).
  void scheduleSave();
  // Call once per frame: writes to disk once the debounce window elapses.
  void update(const AppState& state);
  // Writes immediately, bypassing the debounce (e.g. on app exit, or tests).
  void flush(const AppState& state);

  // Reads session.json if present and well-formed; nullopt otherwise
  // (missing file, parse error, or not a JSON object).
  static std::optional<SettingsSnapshot> load();

private:
  static std::string filePath();
  void writeNow(const AppState& state);

  std::optional<std::chrono::steady_clock::time_point> _pendingAt;
};

} // namespace app
