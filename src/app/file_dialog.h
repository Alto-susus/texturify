// Native Win32 open/save file dialogs (GetOpenFileNameA/GetSaveFileNameA) —
// the app-native equivalent of the JS <input type=file>/download-anchor
// flows. Blocking calls; call from the main thread only.
#pragma once

#include <optional>
#include <string>

namespace app {

// filterName/filterExt e.g. {"STL/OBJ/3MF files", "*.stl;*.obj;*.3mf"}.
// Returns nullopt on cancel.
std::optional<std::string> showOpenFileDialog(const char* title,
                                              const char* filterName,
                                              const char* filterExt);

// defaultName is pre-filled (no path); defaultExt (no dot, e.g. "stl") is
// appended if the user doesn't type one. Returns nullopt on cancel.
std::optional<std::string> showSaveFileDialog(const char* title,
                                              const char* filterName,
                                              const char* filterExt,
                                              const char* defaultName,
                                              const char* defaultExt);

// Whether a native file picker is actually available on this system. Always
// true on Windows/macOS. On Linux, showOpenFileDialog/showSaveFileDialog
// shell out to zenity or kdialog (see file_dialog_linux.cpp) — if neither is
// installed, those calls always silently return nullopt, indistinguishable
// from the user cancelling. Callers should check this first and surface a
// real error instead of a silent no-op.
bool nativeFileDialogAvailable();

} // namespace app
