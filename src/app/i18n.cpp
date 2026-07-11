#include "app/i18n.h"

#include <fstream>
#include <sstream>

#include "app/json.h"

namespace app {

namespace {
// Port of reference/js/i18n.js's TRANSLATIONS registry — each language
// names itself in its own script. Not part of assets/lang/*.json (those are
// pure translated-string documents); this small table is the only place a
// language's own display name and its available codes are known.
struct LangEntry {
  const char* code;
  const char* nativeName;
};
constexpr LangEntry kLanguages[] = {
    {"ru", "\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9"}, // Русский — default (see init())
    {"en", "English"},   {"de", "Deutsch"},     {"it", "Italiano"},
    {"es", "Español"},   {"pt", "Português"},   {"fr", "Français"},
    {"tr", "Türkçe"},    {"ja", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"}, // 日本語
    {"ko", "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4"},                     // 한국어
    {"uk", "\xd0\xa3\xd0\xba\xd1\x80\xd0\xb0\xd1\x97\xd0\xbd\xd1\x81\xd1\x8c\xd0\xba\xd0\xb0"}, // Українська
};
} // namespace

bool I18n::init(const std::string& assetDir) {
  _assetDir = assetDir;
  _available.clear();
  _strings.clear();

  for (const LangEntry& e : kLanguages) {
    std::ifstream f(assetDir + "/lang/" + e.code + ".json", std::ios::binary);
    if (!f) continue;
    std::ostringstream ss;
    ss << f.rdbuf();
    auto parsed = parseJson(ss.str());
    if (!parsed || !parsed->isObject()) continue;

    auto& dst = _strings[e.code];
    for (const auto& [key, val] : parsed->entries())
      if (val.isString()) dst[key] = val.asString();
    _available.push_back({e.code, e.nativeName});
  }

  // Russian is the default UI language (falls back to English if ru.json
  // failed to load for some reason — English remains the ultimate fallback
  // for any missing key regardless of _current, see t() below).
  _current = _strings.count("ru") ? "ru" : "en";
  return _strings.count("en") != 0;
}

bool I18n::setLanguage(const std::string& code) {
  if (!_strings.count(code)) return false;
  _current = code;
  return true;
}

const std::string* I18n::lookupRaw(const std::string& code, const std::string& key) const {
  auto lang = _strings.find(code);
  if (lang == _strings.end()) return nullptr;
  auto it = lang->second.find(key);
  return it == lang->second.end() ? nullptr : &it->second;
}

std::string I18n::t(const std::string& key) const {
  if (const std::string* s = lookupRaw(_current, key)) return *s;
  if (_current != "en")
    if (const std::string* s = lookupRaw("en", key)) return *s;
  return key;
}

std::string I18n::t(const std::string& key,
                    std::initializer_list<std::pair<const char*, std::string>> params) const {
  std::string out = t(key);
  for (const auto& [name, value] : params) {
    const std::string token = std::string("{") + name + "}";
    size_t pos = 0;
    while ((pos = out.find(token, pos)) != std::string::npos) {
      out.replace(pos, token.size(), value);
      pos += value.size();
    }
  }
  return out;
}

} // namespace app
