// macOS native file dialogs (NSOpenPanel/NSSavePanel) — the Cocoa equivalent
// of Windows' GetOpenFileNameA/GetSaveFileNameA (file_dialog_win32.cpp).
// Objective-C++; compiled only on Apple platforms (see CMakeLists.txt).
#include "app/file_dialog.h"

#include <vector>

#import <Cocoa/Cocoa.h>

namespace app {

namespace {

// "*.stl;*.obj;*.3mf" -> {"stl","obj","3mf"} — NSOpenPanel/NSSavePanel's
// allowedFileTypes wants bare extensions, no wildcard/dot.
std::vector<std::string> splitExtensions(const char* ext) {
  std::vector<std::string> out;
  std::string s(ext);
  size_t start = 0;
  while (start <= s.size()) {
    size_t semi = s.find(';', start);
    std::string tok =
        s.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
    size_t dot = tok.find_last_of('.');
    if (dot != std::string::npos) out.push_back(tok.substr(dot + 1));
    if (semi == std::string::npos) break;
    start = semi + 1;
  }
  return out;
}

NSArray<NSString*>* toNSStringArray(const std::vector<std::string>& v) {
  NSMutableArray<NSString*>* arr = [NSMutableArray arrayWithCapacity:v.size()];
  for (const std::string& s : v) [arr addObject:[NSString stringWithUTF8String:s.c_str()]];
  return arr;
}

std::string appendDefaultExt(std::string path, const char* defaultExt) {
  if (!defaultExt || !*defaultExt) return path;
  size_t slash = path.find_last_of('/');
  std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
  if (base.find('.') == std::string::npos) path += std::string(".") + defaultExt;
  return path;
}

} // namespace

std::optional<std::string> showOpenFileDialog(const char* title,
                                              const char* /*filterName*/,
                                              const char* filterExt) {
  @autoreleasepool {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.title = [NSString stringWithUTF8String:title];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    std::vector<std::string> exts = splitExtensions(filterExt);
    if (!exts.empty()) panel.allowedFileTypes = toNSStringArray(exts);
    if ([panel runModal] == NSModalResponseOK) {
      NSURL* url = panel.URLs.firstObject;
      if (url && url.path) return std::string(url.path.UTF8String);
    }
    return std::nullopt;
  }
}

std::optional<std::string> showSaveFileDialog(const char* title,
                                              const char* /*filterName*/,
                                              const char* filterExt,
                                              const char* defaultName,
                                              const char* defaultExt) {
  @autoreleasepool {
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.title = [NSString stringWithUTF8String:title];
    std::vector<std::string> exts = splitExtensions(filterExt);
    if (!exts.empty()) panel.allowedFileTypes = toNSStringArray(exts);
    if (defaultName) panel.nameFieldStringValue = [NSString stringWithUTF8String:defaultName];
    if ([panel runModal] == NSModalResponseOK) {
      NSURL* url = panel.URL;
      if (url && url.path) return appendDefaultExt(std::string(url.path.UTF8String), defaultExt);
    }
    return std::nullopt;
  }
}

} // namespace app
