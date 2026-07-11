// Tiny Objective-C++ helper isolated in its own translation unit (like
// file_dialog_macos.mm) so main.cpp itself can stay a plain .cpp file.
#pragma once

#if defined(__APPLE__)
#include <string>

namespace app {

// Returns the running app bundle's Contents/Resources path (e.g.
// ".../Texturify.app/Contents/Resources"), or "" if not running from a
// bundle (e.g. a bare command-line build during development).
std::string macBundleResourcePath();

} // namespace app
#endif
