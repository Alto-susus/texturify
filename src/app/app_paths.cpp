#include "app/app_paths.h"

#include <cstdlib>
#include <filesystem>

namespace app {

std::string appDataDir() {
  std::string dir;

#if defined(_WIN32)
  const char* appdata = std::getenv("APPDATA");
  dir = appdata ? std::string(appdata) + "\\Texturify" : ".\\Texturify";
#elif defined(__APPLE__)
  const char* home = std::getenv("HOME");
  dir = home ? std::string(home) + "/Library/Application Support/Texturify"
             : "./Texturify";
#else
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  const char* home = std::getenv("HOME");
  if (xdg && *xdg) dir = std::string(xdg) + "/Texturify";
  else if (home) dir = std::string(home) + "/.config/Texturify";
  else dir = "./Texturify";
#endif

  std::error_code ec;
  std::filesystem::create_directories(dir, ec); // best-effort, no-op if it exists
  return dir;
}

} // namespace app
