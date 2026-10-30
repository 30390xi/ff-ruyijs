#pragma once
#include "document_model/model.h"

namespace ruyijs::document_model {
struct GeometryNode {
  bool element = false, graphics = false, textContent = false, connected = false;
  DomString localName, text;
  std::vector<AttributeSnapshot> attributes;
  // A text content element's children in order: a text node's data (child
  // -1), or an element child as its index in the children list.
  struct TextSegment { int child = -1; DomString text; };
  std::vector<TextSegment> segments;
  void* factory = nullptr;  // Non-owning service of the original Wrapper.factory realm.
};
}
