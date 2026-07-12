#include "app/file_dialog.h"

#include <windows.h>
#include <commdlg.h>

#include <cstring>
#include <vector>

namespace app {

namespace {

// Builds the double-null-terminated "Description\0*.ext\0\0" filter string
// GetOpenFileNameA/GetSaveFileNameA expect.
std::vector<char> makeFilter(const char* name, const char* ext) {
  std::vector<char> f;
  f.insert(f.end(), name, name + std::strlen(name) + 1);
  f.insert(f.end(), ext, ext + std::strlen(ext) + 1);
  f.push_back('\0');
  return f;
}

} // namespace

std::optional<std::string> showOpenFileDialog(const char* title,
                                              const char* filterName,
                                              const char* filterExt) {
  char path[MAX_PATH] = {0};
  std::vector<char> filter = makeFilter(filterName, filterExt);

  OPENFILENAMEA ofn;
  std::memset(&ofn, 0, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = nullptr;
  ofn.lpstrFilter = filter.data();
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrTitle = title;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

  if (GetOpenFileNameA(&ofn)) return std::string(path);
  return std::nullopt;
}

std::optional<std::string> showSaveFileDialog(const char* title,
                                              const char* filterName,
                                              const char* filterExt,
                                              const char* defaultName,
                                              const char* defaultExt) {
  char path[MAX_PATH] = {0};
  if (defaultName) std::strncpy(path, defaultName, MAX_PATH - 1);
  std::vector<char> filter = makeFilter(filterName, filterExt);

  OPENFILENAMEA ofn;
  std::memset(&ofn, 0, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = nullptr;
  ofn.lpstrFilter = filter.data();
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrTitle = title;
  ofn.lpstrDefExt = defaultExt;
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

  if (GetSaveFileNameA(&ofn)) return std::string(path);
  return std::nullopt;
}

bool nativeFileDialogAvailable() { return true; }

} // namespace app
