// Minimal JSON value type + parser/serializer for the app layer's flat,
// known-shape documents (settings snapshots, project files, session
// autosave). Not a general-purpose library: no comments, no unicode escapes
// beyond \uXXXX -> UTF-8, no big-number precision guarantees beyond double.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace app {

class JsonValue;
using JsonArray = std::vector<JsonValue>;
// Insertion-ordered object (JSON key order matters for human-readable diffs
// and matches the order fields are written in toJson()).
using JsonObjectEntries = std::vector<std::pair<std::string, JsonValue>>;

class JsonValue {
public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  JsonValue() : _type(Type::Null) {}
  JsonValue(std::nullptr_t) : _type(Type::Null) {}
  JsonValue(bool b) : _type(Type::Bool), _bool(b) {}
  JsonValue(double n) : _type(Type::Number), _num(n) {}
  JsonValue(int n) : _type(Type::Number), _num(n) {}
  JsonValue(int64_t n) : _type(Type::Number), _num((double)n) {}
  JsonValue(const char* s) : _type(Type::String), _str(s) {}
  JsonValue(std::string s) : _type(Type::String), _str(std::move(s)) {}
  JsonValue(JsonArray a) : _type(Type::Array), _arr(std::move(a)) {}
  JsonValue(JsonObjectEntries o) : _type(Type::Object), _obj(std::move(o)) {}

  static JsonValue object() { return JsonValue(JsonObjectEntries{}); }
  static JsonValue array() { return JsonValue(JsonArray{}); }

  Type type() const { return _type; }
  bool isNull() const { return _type == Type::Null; }
  bool isObject() const { return _type == Type::Object; }
  bool isArray() const { return _type == Type::Array; }
  bool isNumber() const { return _type == Type::Number; }
  bool isString() const { return _type == Type::String; }
  bool isBool() const { return _type == Type::Bool; }

  double asNumber(double fallback = 0) const { return _type == Type::Number ? _num : fallback; }
  bool asBool(bool fallback = false) const { return _type == Type::Bool ? _bool : fallback; }
  const std::string& asString() const { return _str; }
  const JsonArray& asArray() const { return _arr; }

  // Object helpers (linear scan — objects here are small, <30 keys).
  void set(const std::string& key, JsonValue v) {
    for (auto& [k, val] : _obj)
      if (k == key) { val = std::move(v); return; }
    _obj.emplace_back(key, std::move(v));
  }
  const JsonValue* find(const std::string& key) const {
    for (const auto& [k, v] : _obj)
      if (k == key) return &v;
    return nullptr;
  }
  bool has(const std::string& key) const { return find(key) != nullptr; }
  // Full key/value list for generic iteration (e.g. i18n's flat
  // string-only documents, where the key set is large and unknown ahead of
  // time — unlike settings snapshots/project files, which only ever look up
  // a fixed set of known keys via find()/getString()/etc.).
  const JsonObjectEntries& entries() const { return _obj; }

  void push_back(JsonValue v) { _arr.push_back(std::move(v)); }

  // Typed getters with a default when the key is missing/null/wrong type.
  double getNumber(const std::string& key, double fallback = 0) const {
    const JsonValue* v = find(key);
    return v && v->isNumber() ? v->_num : fallback;
  }
  bool getBool(const std::string& key, bool fallback = false) const {
    const JsonValue* v = find(key);
    return v && v->isBool() ? v->_bool : fallback;
  }
  std::string getString(const std::string& key, const std::string& fallback = "") const {
    const JsonValue* v = find(key);
    return v && v->isString() ? v->_str : fallback;
  }
  // nullopt when the key is absent OR explicitly JSON null (both mean
  // "no value" for our optional<double> settings fields).
  std::optional<double> getOptNumber(const std::string& key) const {
    const JsonValue* v = find(key);
    if (!v || v->isNull()) return std::nullopt;
    return v->isNumber() ? std::optional<double>(v->_num) : std::nullopt;
  }

  std::string dump(int indent = 0) const;

private:
  Type _type = Type::Null;
  bool _bool = false;
  double _num = 0;
  std::string _str;
  JsonArray _arr;
  JsonObjectEntries _obj;
};

// Returns nullopt on parse error.
std::optional<JsonValue> parseJson(const std::string& text);

} // namespace app
