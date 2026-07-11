// Port of reference/js/i18n.js — flat dotted-key string lookup with English
// fallback and literal "{name}" placeholder interpolation. Language string
// data comes from assets/lang/*.json (already converted 1:1 from
// reference/js/i18n/*.js's per-language key/value objects).
//
// Unlike the web app (lazy dynamic `import()` per language, since fetching a
// JS module over the network is the expensive part there), this loads every
// available language's JSON up front at init() — they're small local files,
// and eager loading means the language-picker can show every language's own
// native name without first switching to it.
#pragma once

#include <initializer_list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace app {

struct LanguageInfo {
  std::string code;       // "en", "de", "ja", ... (assets/lang/<code>.json)
  std::string nativeName; // this language's own name, e.g. "Deutsch", "日本語"
};

class I18n {
public:
  // Loads every language listed in kLanguages (see i18n.cpp) from
  // <assetDir>/lang/<code>.json. Returns false only if English itself fails
  // to load (mirrors main.js's initLang() `enFailed` case) — every other
  // language is best-effort (a missing/corrupt file is just dropped from
  // available()), matching setLang()'s per-language load tolerance.
  bool init(const std::string& assetDir);

  // No-op (returns false) if `code` didn't load successfully in init().
  bool setLanguage(const std::string& code);
  const std::string& currentLanguage() const { return _current; }

  // Successfully-loaded languages, in kLanguages order (English first).
  const std::vector<LanguageInfo>& available() const { return _available; }

  // t(key): current language -> English -> the raw key itself, matching
  // main.js's `strings[key] ?? fallback[key] ?? key`.
  std::string t(const std::string& key) const;
  // t(key, {{"n", "3"}}): same lookup, then replaces every literal "{name}"
  // occurrence with its paired value (main.js's `str.replaceAll(`{${k}}`, v)`
  // — no pluralization/ICU, just substring replace).
  std::string t(const std::string& key,
                std::initializer_list<std::pair<const char*, std::string>> params) const;

private:
  const std::string* lookupRaw(const std::string& code, const std::string& key) const;

  std::string _assetDir;
  std::string _current = "en";
  std::vector<LanguageInfo> _available;
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>> _strings;
};

} // namespace app
