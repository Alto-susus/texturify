#include "app/json.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace app {

namespace {

void dumpString(std::string& out, const std::string& s) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += (char)c;
        }
    }
  }
  out += '"';
}

void dumpNumber(std::string& out, double n) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.17g", n);
  out += buf;
}

void indentLine(std::string& out, int indent) { out.append((size_t)indent * 2, ' '); }

// Parser --------------------------------------------------------------------

class Parser {
public:
  Parser(const char* begin, const char* end) : _p(begin), _end(end) {}

  std::optional<JsonValue> parse() {
    skipWs();
    JsonValue v;
    if (!parseValue(v)) return std::nullopt;
    skipWs();
    return v;
  }

private:
  const char* _p;
  const char* _end;

  void skipWs() {
    while (_p < _end && (*_p == ' ' || *_p == '\t' || *_p == '\n' || *_p == '\r')) _p++;
  }
  bool eat(char c) {
    skipWs();
    if (_p < _end && *_p == c) { _p++; return true; }
    return false;
  }
  bool peek(char c) {
    skipWs();
    return _p < _end && *_p == c;
  }
  bool literal(const char* lit) {
    size_t n = std::strlen(lit);
    if ((size_t)(_end - _p) < n) return false;
    for (size_t i = 0; i < n; i++)
      if (_p[i] != lit[i]) return false;
    _p += n;
    return true;
  }

  bool parseValue(JsonValue& out) {
    skipWs();
    if (_p >= _end) return false;
    char c = *_p;
    if (c == '{') return parseObject(out);
    if (c == '[') return parseArray(out);
    if (c == '"') return parseString(out);
    if (c == 't') { if (literal("true")) { out = JsonValue(true); return true; } return false; }
    if (c == 'f') { if (literal("false")) { out = JsonValue(false); return true; } return false; }
    if (c == 'n') { if (literal("null")) { out = JsonValue(nullptr); return true; } return false; }
    if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
    return false;
  }

  bool parseObject(JsonValue& out) {
    if (!eat('{')) return false;
    JsonValue obj = JsonValue::object();
    skipWs();
    if (eat('}')) { out = obj; return true; }
    while (true) {
      skipWs();
      JsonValue key;
      if (!peek('"') || !parseString(key)) return false;
      if (!eat(':')) return false;
      JsonValue val;
      if (!parseValue(val)) return false;
      obj.set(key.asString(), std::move(val));
      skipWs();
      if (eat(',')) continue;
      if (eat('}')) break;
      return false;
    }
    out = obj;
    return true;
  }

  bool parseArray(JsonValue& out) {
    if (!eat('[')) return false;
    JsonValue arr = JsonValue::array();
    skipWs();
    if (eat(']')) { out = arr; return true; }
    while (true) {
      JsonValue val;
      if (!parseValue(val)) return false;
      arr.push_back(std::move(val));
      skipWs();
      if (eat(',')) continue;
      if (eat(']')) break;
      return false;
    }
    out = arr;
    return true;
  }

  bool parseString(JsonValue& out) {
    if (!eat('"')) return false;
    std::string s;
    while (_p < _end && *_p != '"') {
      char c = *_p++;
      if (c == '\\') {
        if (_p >= _end) return false;
        char e = *_p++;
        switch (e) {
          case '"': s += '"'; break;
          case '\\': s += '\\'; break;
          case '/': s += '/'; break;
          case 'n': s += '\n'; break;
          case 'r': s += '\r'; break;
          case 't': s += '\t'; break;
          case 'b': s += '\b'; break;
          case 'f': s += '\f'; break;
          case 'u': {
            if (_end - _p < 4) return false;
            unsigned code = 0;
            for (int i = 0; i < 4; i++) {
              char h = _p[i];
              code <<= 4;
              if (h >= '0' && h <= '9') code |= (unsigned)(h - '0');
              else if (h >= 'a' && h <= 'f') code |= (unsigned)(h - 'a' + 10);
              else if (h >= 'A' && h <= 'F') code |= (unsigned)(h - 'A' + 10);
              else return false;
            }
            _p += 4;
            // UTF-8 encode (BMP only — sufficient for our own JSON output).
            if (code < 0x80) {
              s += (char)code;
            } else if (code < 0x800) {
              s += (char)(0xC0 | (code >> 6));
              s += (char)(0x80 | (code & 0x3F));
            } else {
              s += (char)(0xE0 | (code >> 12));
              s += (char)(0x80 | ((code >> 6) & 0x3F));
              s += (char)(0x80 | (code & 0x3F));
            }
            break;
          }
          default: return false;
        }
      } else {
        s += c;
      }
    }
    if (!eat('"')) return false;
    out = JsonValue(std::move(s));
    return true;
  }

  bool parseNumber(JsonValue& out) {
    const char* start = _p;
    if (_p < _end && *_p == '-') _p++;
    while (_p < _end && std::isdigit((unsigned char)*_p)) _p++;
    if (_p < _end && *_p == '.') {
      _p++;
      while (_p < _end && std::isdigit((unsigned char)*_p)) _p++;
    }
    if (_p < _end && (*_p == 'e' || *_p == 'E')) {
      _p++;
      if (_p < _end && (*_p == '+' || *_p == '-')) _p++;
      while (_p < _end && std::isdigit((unsigned char)*_p)) _p++;
    }
    if (_p == start) return false;
    std::string tok(start, _p);
    out = JsonValue(std::strtod(tok.c_str(), nullptr));
    return true;
  }
};

} // namespace

std::string JsonValue::dump(int indent) const {
  std::string out;
  switch (_type) {
    case Type::Null: out = "null"; break;
    case Type::Bool: out = _bool ? "true" : "false"; break;
    case Type::Number: dumpNumber(out, _num); break;
    case Type::String: dumpString(out, _str); break;
    case Type::Array: {
      if (_arr.empty()) { out = "[]"; break; }
      out = "[\n";
      for (size_t i = 0; i < _arr.size(); i++) {
        indentLine(out, indent + 1);
        out += _arr[i].dump(indent + 1);
        if (i + 1 < _arr.size()) out += ',';
        out += '\n';
      }
      indentLine(out, indent);
      out += ']';
      break;
    }
    case Type::Object: {
      if (_obj.empty()) { out = "{}"; break; }
      out = "{\n";
      for (size_t i = 0; i < _obj.size(); i++) {
        indentLine(out, indent + 1);
        dumpString(out, _obj[i].first);
        out += ": ";
        out += _obj[i].second.dump(indent + 1);
        if (i + 1 < _obj.size()) out += ',';
        out += '\n';
      }
      indentLine(out, indent);
      out += '}';
      break;
    }
  }
  return out;
}

std::optional<JsonValue> parseJson(const std::string& text) {
  Parser p(text.data(), text.data() + text.size());
  return p.parse();
}

} // namespace app
