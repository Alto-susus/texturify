// Linux native file dialogs. No single native toolkit ships on every
// desktop, so this shells out to whichever picker is installed — zenity
// (GNOME/Ubuntu, the .deb's Recommends: dependency) or kdialog (KDE) — via
// popen, matching the blocking/synchronous contract showOpenFileDialog/
// showSaveFileDialog already have on Windows (file_dialog_win32.cpp). Returns
// nullopt if neither tool is present.
#include "app/file_dialog.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace app {

namespace {

bool commandExists(const char* name) {
  std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
  return std::system(cmd.c_str()) == 0;
}

// "*.stl;*.obj;*.3mf" -> "*.stl *.obj *.3mf" (zenity --file-filter syntax).
std::string toSpaceSeparated(const char* ext) {
  std::string s(ext);
  for (char& c : s)
    if (c == ';') c = ' ';
  return s;
}

// Single-quotes a string for /bin/sh, escaping embedded single quotes.
// Titles/filters here are all hardcoded literals from call sites, not
// external input, but this keeps the popen command construction safe
// regardless.
std::string shellQuote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  out += "'";
  return out;
}

std::optional<std::string> runAndReadLine(const std::string& cmd) {
  std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(cmd.c_str(), "r"), pclose);
  if (!pipe) return std::nullopt;
  std::string result;
  std::array<char, 4096> buf;
  size_t n;
  while ((n = fread(buf.data(), 1, buf.size(), pipe.get())) > 0)
    result.append(buf.data(), n);
  while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
    result.pop_back();
  if (result.empty()) return std::nullopt;
  return result;
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
                                              const char* filterName,
                                              const char* filterExt) {
  const std::string filter = std::string(filterName) + " | " + toSpaceSeparated(filterExt);
  if (commandExists("zenity")) {
    std::string cmd = "zenity --file-selection --title=" + shellQuote(title ? title : "") +
                       " --file-filter=" + shellQuote(filter) + " 2>/dev/null";
    return runAndReadLine(cmd);
  }
  if (commandExists("kdialog")) {
    std::string pattern = "(" + toSpaceSeparated(filterExt) + ")";
    std::string cmd = "kdialog --title " + shellQuote(title ? title : "") +
                       " --getopenfilename . " + shellQuote(pattern) + " 2>/dev/null";
    return runAndReadLine(cmd);
  }
  return std::nullopt;
}

std::optional<std::string> showSaveFileDialog(const char* title,
                                              const char* filterName,
                                              const char* filterExt,
                                              const char* defaultName,
                                              const char* defaultExt) {
  const std::string filter = std::string(filterName) + " | " + toSpaceSeparated(filterExt);
  std::optional<std::string> path;
  if (commandExists("zenity")) {
    std::string cmd = "zenity --file-selection --save --confirm-overwrite --title=" +
                       shellQuote(title ? title : "");
    if (defaultName) cmd += " --filename=" + shellQuote(defaultName);
    cmd += " --file-filter=" + shellQuote(filter) + " 2>/dev/null";
    path = runAndReadLine(cmd);
  } else if (commandExists("kdialog")) {
    std::string pattern = "(" + toSpaceSeparated(filterExt) + ")";
    std::string startPath = defaultName ? defaultName : ".";
    std::string cmd = "kdialog --title " + shellQuote(title ? title : "") +
                       " --getsavefilename " + shellQuote(startPath) + " " +
                       shellQuote(pattern) + " 2>/dev/null";
    path = runAndReadLine(cmd);
  }
  if (!path) return std::nullopt;
  return appendDefaultExt(*path, defaultExt);
}

bool nativeFileDialogAvailable() { return commandExists("zenity") || commandExists("kdialog"); }

} // namespace app
