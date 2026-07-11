#include "core/xml_mini.h"

#include <cctype>

namespace core::xml {

namespace {

std::string localOf(const std::string& name) {
  auto pos = name.rfind(':');
  return pos == std::string::npos ? name : name.substr(pos + 1);
}

std::string decodeEntities(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] != '&') {
      out += s[i];
      continue;
    }
    auto end = s.find(';', i);
    if (end == std::string_view::npos) {
      out += s[i];
      continue;
    }
    std::string_view ent = s.substr(i + 1, end - i - 1);
    if (ent == "lt") out += '<';
    else if (ent == "gt") out += '>';
    else if (ent == "amp") out += '&';
    else if (ent == "quot") out += '"';
    else if (ent == "apos") out += '\'';
    else if (!ent.empty() && ent[0] == '#') {
      long code = 0;
      if (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X'))
        code = std::strtol(std::string(ent.substr(2)).c_str(), nullptr, 16);
      else
        code = std::strtol(std::string(ent.substr(1)).c_str(), nullptr, 10);
      // UTF-8 encode
      if (code < 0x80) out += static_cast<char>(code);
      else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
      } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
      } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
      }
    }
    i = end;
  }
  return out;
}

struct Parser {
  std::string_view s;
  size_t p = 0;
  std::string error;

  bool eof() const { return p >= s.size(); }
  char cur() const { return s[p]; }

  void skipWs() {
    while (!eof() && std::isspace(static_cast<unsigned char>(s[p]))) p++;
  }

  bool startsWith(std::string_view prefix) const {
    return s.compare(p, prefix.size(), prefix) == 0;
  }

  // Skips <?...?>, <!--...-->, <!DOCTYPE...>, <![CDATA[...]]> and text.
  // Positions at the next '<' that begins an element tag, or EOF.
  void skipToTag() {
    while (!eof()) {
      size_t lt = s.find('<', p);
      if (lt == std::string_view::npos) { p = s.size(); return; }
      p = lt;
      if (startsWith("<?")) {
        size_t end = s.find("?>", p);
        p = (end == std::string_view::npos) ? s.size() : end + 2;
      } else if (startsWith("<!--")) {
        size_t end = s.find("-->", p);
        p = (end == std::string_view::npos) ? s.size() : end + 3;
      } else if (startsWith("<![CDATA[")) {
        size_t end = s.find("]]>", p);
        p = (end == std::string_view::npos) ? s.size() : end + 3;
      } else if (startsWith("<!")) {
        size_t end = s.find('>', p);
        p = (end == std::string_view::npos) ? s.size() : end + 1;
      } else {
        return; // element open or close tag
      }
    }
  }

  std::string readName() {
    size_t start = p;
    while (!eof()) {
      char c = s[p];
      if (std::isspace(static_cast<unsigned char>(c)) || c == '>' || c == '/' ||
          c == '=')
        break;
      p++;
    }
    return std::string(s.substr(start, p - start));
  }

  // Parses one element (opening tag already peeked: s[p] == '<', name follows).
  std::unique_ptr<Element> parseElement() {
    p++; // '<'
    auto el = std::make_unique<Element>();
    el->name = readName();
    el->localName = localOf(el->name);

    // attributes
    for (;;) {
      skipWs();
      if (eof()) { error = "unexpected EOF in tag"; return nullptr; }
      if (cur() == '/') {
        p++;
        if (!eof() && cur() == '>') { p++; return el; } // self-closing
        error = "malformed self-closing tag";
        return nullptr;
      }
      if (cur() == '>') { p++; break; }
      std::string attrName = readName();
      if (attrName.empty()) { error = "malformed attribute"; return nullptr; }
      skipWs();
      std::string value;
      if (!eof() && cur() == '=') {
        p++;
        skipWs();
        if (eof() || (cur() != '"' && cur() != '\'')) {
          error = "unquoted attribute value";
          return nullptr;
        }
        char quote = cur();
        p++;
        size_t end = s.find(quote, p);
        if (end == std::string_view::npos) { error = "unterminated attribute"; return nullptr; }
        value = decodeEntities(s.substr(p, end - p));
        p = end + 1;
      }
      el->attributes.emplace_back(std::move(attrName), std::move(value));
    }

    // children until matching close tag
    for (;;) {
      skipToTag();
      if (eof()) { error = "unexpected EOF, unclosed <" + el->name + ">"; return nullptr; }
      if (startsWith("</")) {
        size_t end = s.find('>', p);
        if (end == std::string_view::npos) { error = "unterminated close tag"; return nullptr; }
        p = end + 1;
        return el;
      }
      auto child = parseElement();
      if (!child) return nullptr;
      child->parent = el.get();
      el->children.push_back(std::move(child));
    }
  }
};

} // namespace

std::string Element::attr(std::string_view attrName) const {
  for (const auto& [n, v] : attributes)
    if (n == attrName) return v;
  return {};
}

std::string Element::attrLocal(std::string_view local) const {
  for (const auto& [n, v] : attributes) {
    auto pos = n.rfind(':');
    std::string_view ln = (pos == std::string::npos)
                              ? std::string_view(n)
                              : std::string_view(n).substr(pos + 1);
    if (ln == local) return v;
  }
  return {};
}

bool Element::hasAttr(std::string_view attrName) const {
  for (const auto& [n, v] : attributes)
    if (n == attrName) return true;
  return false;
}

void Element::findAll(std::string_view local, std::vector<const Element*>& out) const {
  for (const auto& c : children) {
    if (c->localName == local) out.push_back(c.get());
    c->findAll(local, out);
  }
}

std::vector<const Element*> Element::findAll(std::string_view local) const {
  std::vector<const Element*> out;
  findAll(local, out);
  return out;
}

const Element* Element::findFirst(std::string_view local) const {
  for (const auto& c : children) {
    if (c->localName == local) return c.get();
    if (const Element* r = c->findFirst(local)) return r;
  }
  return nullptr;
}

Document parse(std::string_view text) {
  Document doc;
  Parser parser{text};
  parser.skipToTag();
  if (parser.eof()) {
    doc.error = "no root element";
    return doc;
  }
  doc.root = parser.parseElement();
  if (!doc.root) doc.error = parser.error.empty() ? "parse error" : parser.error;
  return doc;
}

} // namespace core::xml
