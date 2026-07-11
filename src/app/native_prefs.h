// Small standalone native-app preference file
// (%APPDATA%\Texturify\prefs.json), for the handful of settings that must
// persist independently of undo/redo, .texturify project files, and
// session-autosave's settings snapshot: the UI language choice (main.js's
// localStorage['stlt-lang']) and the welcome-popup dismiss stamp (main.js's
// localStorage['stlt-welcome-seen']). Neither belongs in SettingsSnapshot —
// main.js keeps both as separate top-level localStorage keys precisely
// because they aren't part of the mesh/texture "settings" undo/redo tracks.
#pragma once

#include <string>

namespace app {

struct NativePrefs {
  std::string lang;              // "" = not yet chosen (caller defaults to "ru", app::I18n::init())
  std::string welcomeSeenVersion; // "" = welcome popup never dismissed
};

NativePrefs loadNativePrefs();
void saveNativePrefs(const NativePrefs& prefs);

} // namespace app
