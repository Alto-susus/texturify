// Cross-platform per-user app-data directory, shared by native_prefs.cpp and
// session_autosave.cpp (both previously had their own copy-pasted, Windows-
// only %APPDATA% resolution).
#pragma once

#include <string>

namespace app {

// Returns (and creates, best-effort) the app's per-user data directory:
//   Windows: %APPDATA%\Texturify
//   macOS:   ~/Library/Application Support/Texturify
//   Linux:   $XDG_CONFIG_HOME/Texturify, or ~/.config/Texturify
// Falls back to "./Texturify" if the relevant environment variable is unset
// (matches the previous Windows-only behavior's fallback).
std::string appDataDir();

} // namespace app
