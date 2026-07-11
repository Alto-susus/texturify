#include "app/native_prefs.h"

#include <fstream>
#include <sstream>

#include "app/app_paths.h"
#include "app/json.h"

namespace app {

namespace {
std::string prefsPath() { return appDataDir() + "/prefs.json"; }
} // namespace

NativePrefs loadNativePrefs() {
  NativePrefs prefs;
  std::ifstream f(prefsPath(), std::ios::binary);
  if (!f) return prefs;
  std::ostringstream ss;
  ss << f.rdbuf();
  auto parsed = parseJson(ss.str());
  if (!parsed || !parsed->isObject()) return prefs;
  prefs.lang = parsed->getString("lang");
  prefs.welcomeSeenVersion = parsed->getString("welcomeSeenVersion");
  return prefs;
}

void saveNativePrefs(const NativePrefs& prefs) {
  JsonValue root = JsonValue::object();
  root.set("lang", prefs.lang);
  root.set("welcomeSeenVersion", prefs.welcomeSeenVersion);
  std::ofstream f(prefsPath(), std::ios::binary | std::ios::trunc);
  if (!f) return; // best-effort, matches main.js's try/catch-and-ignore
  const std::string text = root.dump();
  f.write(text.data(), (std::streamsize)text.size());
}

} // namespace app
