// Minimal non-validating XML parser — just enough for 3MF .model files
// (elements, attributes, self-closing tags, comments, CDATA, prolog,
// basic entities). Element/attribute lookups are by local name (the part
// after any namespace prefix), which matches how real-world 3MF files use
// the core/production namespaces.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace core::xml {

struct Element {
  std::string name;       // as written, possibly "prefix:local"
  std::string localName;  // part after ':'
  std::vector<std::pair<std::string, std::string>> attributes; // name → value
  std::vector<std::unique_ptr<Element>> children;
  Element* parent = nullptr;

  // Attribute by exact name, or empty string.
  std::string attr(std::string_view attrName) const;
  // Attribute by local name (ignores any "prefix:"), or empty string.
  std::string attrLocal(std::string_view local) const;
  bool hasAttr(std::string_view attrName) const;

  // All descendants (depth-first, document order) whose localName matches.
  void findAll(std::string_view local, std::vector<const Element*>& out) const;
  std::vector<const Element*> findAll(std::string_view local) const;
  // First direct or nested descendant with matching localName, or nullptr.
  const Element* findFirst(std::string_view local) const;
};

struct Document {
  std::unique_ptr<Element> root;
  std::string error; // non-empty on parse failure
};

Document parse(std::string_view text);

} // namespace core::xml
