#include "web_geometry/svg.h"
#include "web_geometry/system_font.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ruyijs::web_geometry {
namespace {
bool Read(State& s, JS::HandleObject node, SvgInput* input, JS::MutableHandleObject parent,
           JS::MutableHandleObjectVector children) {
  JS::RootedValue value(s.cx, JS::ObjectValue(*node));
  if (!s.svgPorts.node(s.svgPorts.context, value, &input->node, parent, children)) return false;
  if (input->node.element && !s.svgPorts.style(s.svgPorts.context, node, &input->style)) return false;
  auto transform = input->style.Get("transform");
  if (transform.empty() || transform == "none") transform = SvgAttribute(*input, "transform");
  input->transform = SvgTransform(transform);
  return true;
}
FontInput TextFont(const SvgInput& input, float scale, const svg::SvgMatrix& transform) {
  FontInput font;
  font.family = input.style.Get("font-family");
  font.size = input.style.fontSize;
  font.italic = input.style.Get("font-style") != "normal";
  const auto weight = input.style.Get("font-weight");
  if (!weight.empty()) font.weight = unsigned(std::strtoul(weight.c_str(), nullptr, 10));
  // The inherited content language from the cascade (own lang/xml:lang or an
  // ancestor's).
  font.language = input.style.Get("-x-lang");
  if (font.language == "-x-none") font.language.clear();
  font.scale = scale;
  // The text frame's style font size, before SVGTextFrame's size scale factor.
  font.optimizeSpeed = TextRunOptimizeSpeed(input.style.Get("text-rendering"), font.size);
  // letter-spacing: a computed length in px ("normal" is 0).
  const auto spacing = input.style.Get("letter-spacing");
  char* end = nullptr;
  const double value = spacing.empty() || spacing == "normal" ? 0 : std::strtod(spacing.c_str(), &end);
  if (std::isfinite(value) && (!end || *end == 0 || std::strcmp(end, "px") == 0)) font.letterSpacing = value;
  font.transform = transform;
  return font;
}
// The laid-out text of one <text> subtree (SVGTextFrame::DoGlyphPositioning):
// its text nodes and tspan/a children in order on one line, each run in its own
// element's font. An x/y attribute starts a new position, dx/dy shift it.
struct TextFlow {
  State& s;
  float scale;
  svg::SvgMatrix transform;
  double x = 0, y = 0;
  svg::SvgBounds bounds, transformed;
  svg::SvgTextMeasurement* out;
  double length = 0;
  unsigned depth = 0;
  bool Run(const SvgInput& input, const std::u16string& text) {
    if (text.empty()) return true;
    FontInput font = TextFont(input, scale, transform);
    font.x = x; font.y = y;
    svg::SvgTextMeasurement run;
    if (!s.fonts.Measure(text, font, &run))
      return WebKit::Fail(s.cx, "SVG system-font measurement exceeds supported input or font resources");
    if (!run.characters.empty() && run.box[2] + run.box[3] > 0) {
      bounds.Include(run.box);
      transformed.Include(run.transformed);
    }
    out->characters.insert(out->characters.end(), run.characters.begin(), run.characters.end());
    x += run.length; length += run.length;
    return true;
  }
  bool Element(const SvgInput& input, JS::HandleObjectVector children) {
    if (++depth > 64) return WebKit::Fail(s.cx, "SVG text nesting budget exceeded");
    for (const auto& segment : input.node.segments) {
      if (segment.child < 0) { if (!Run(input, segment.text)) return false; continue; }
      if (size_t(segment.child) >= children.length()) continue;
      JS::RootedObject element(s.cx, children[segment.child]);
      SvgInput child = input;
      JS::RootedObject parent(s.cx);
      JS::RootedObjectVector grandchildren(s.cx);
      if (!Read(s, element, &child, &parent, &grandchildren)) return false;
      if (!child.node.connected || child.style.Get("display") == "none" || !child.node.textContent) continue;
      Position(child);
      if (!Element(child, grandchildren)) return false;
    }
    --depth;
    return true;
  }
  void Position(const SvgInput& input) {
    if (!SvgAttribute(input, "x").empty()) x = SvgLength(input, "x", input.viewportWidth);
    if (!SvgAttribute(input, "y").empty()) y = SvgLength(input, "y", input.viewportHeight);
    x += SvgLength(input, "dx", input.viewportWidth);
    y += SvgLength(input, "dy", input.viewportHeight);
  }
};
bool Text(State& s, const SvgInput& input, float scale, JS::HandleObjectVector children,
          svg::SvgTextMeasurement* out) {
  *out = {};
  TextFlow flow{s, scale, input.transform};
  flow.out = out;
  flow.x = SvgLength(input, "x", input.viewportWidth) + SvgLength(input, "dx", input.viewportWidth);
  flow.y = SvgLength(input, "y", input.viewportHeight) + SvgLength(input, "dy", input.viewportHeight);
  if (input.node.segments.empty()) {
    if (!flow.Run(input, input.node.text)) return false;
  } else if (!flow.Element(input, children)) return false;
  out->box = flow.bounds.box;
  out->transformed = flow.transformed.box;
  out->length = float(flow.length);
  // textLength="" with lengthAdjust="spacing" (the default): the difference
  // from the natural length is spread over the gaps between characters; the
  // computed text length stays the natural one (SVGTextFrame::DoGlyphPositioning).
  const auto textLength = SvgAttribute(input, "textLength");
  if (!textLength.empty() && out->characters.size() > 1) {
    const double expected = SvgLength(input, "textLength", input.viewportWidth, -1);
    const auto adjust = SvgAttribute(input, "lengthAdjust");
    if (expected >= 0 && (adjust.empty() || adjust == "spacing" || adjust == "spacingAndGlyphs")) {
      const double origin = out->characters.front()[0];
      if (adjust == "spacingAndGlyphs") {
        const double factor = out->length > 0 ? expected / out->length : 1;
        for (auto& box : out->characters) {
          box[0] = float(origin + (box[0] - origin) * factor); box[2] = float(box[2] * factor);
        }
        out->box[0] = float(origin + (out->box[0] - origin) * factor);
        out->box[2] = float(out->box[2] * factor);
      } else {
        const double step = (expected - out->length) / double(out->characters.size() - 1);
        for (size_t i = 1; i < out->characters.size(); ++i)
          out->characters[i][0] = float(out->characters[i][0] + step * double(i));
        out->box[2] = float(std::max(0.0, double(out->box[2]) + step * double(out->characters.size() - 1)));
      }
      out->transformed = input.transform.Apply(out->box);
    }
  }
  const auto anchor = input.style.Get("text-anchor");
  const float shift = anchor == "middle" ? out->length/2 : anchor == "end" ? out->length : 0;
  out->box[0] -= shift;
  for (auto& box : out->characters) box[0] -= shift;
  return true;
}
bool Visit(State& s, JS::HandleObject element, SvgInput input, const SvgOptions& options,
           svg::SvgMatrix matrix, unsigned depth, unsigned* visited,
           svg::SvgTextMeasurement* out, bool* present) {
  if (depth >= 128 || ++*visited > 4096) return WebKit::Fail(s.cx, "SVG geometry tree budget exceeded");
  JS::RootedObject parent(s.cx);
  JS::RootedObjectVector children(s.cx);
  if (!Read(s, element, &input, &parent, &children)) return false;
  *present = false;
  if (!input.node.connected || input.style.Get("display") == "none") return true;
  matrix = matrix.Then(input.transform);
  if (input.node.localName == u"svg") {
    input.viewportWidth = SvgLength(input, "width", input.viewportWidth, input.viewportWidth);
    input.viewportHeight = SvgLength(input, "height", input.viewportHeight, input.viewportHeight);
  }
  if (input.node.textContent) {
    if (!Text(s, input, matrix.Scale(), children, out)) return false;
    *present = !out->characters.empty();
    return true;
  }
  if (input.node.localName != u"g" && input.node.localName != u"svg" && input.node.localName != u"a" &&
      input.node.localName != u"switch" && input.node.localName != u"symbol" && input.node.localName != u"defs") {
    if (!input.node.graphics) return true;
    if (!SvgShape(input, options, &out->box, present))
      return WebKit::Fail(s.cx, "SVG shape geometry is not yet supported for this input");
    return true;
  }
  svg::SvgBounds bounds;
  for (size_t i = 0; i < children.length(); ++i) {
    JS::RootedObject child(s.cx, children[i]);
    SvgInput childInput = input;
    JS::RootedObject childParent(s.cx);
    JS::RootedObjectVector grandchildren(s.cx);
    if (!Read(s, child, &childInput, &childParent, &grandchildren)) return false;
    if (!childInput.node.graphics) continue;
    const auto transform = childInput.transform;
    const bool isText = childInput.node.textContent;
    svg::SvgTextMeasurement measured;
    bool included = false;
    if (!Visit(s, child, std::move(childInput), options, matrix, depth+1, visited, &measured, &included)) return false;
    if (included) bounds.Include(isText ? measured.transformed : transform.Apply(measured.box));
  }
  *present = bounds.present; out->box = bounds.box; return true;
}
}
bool SvgMeasure(State& s, JS::HandleObject element, const SvgOptions& options,
                 svg::SvgTextMeasurement* out, bool* rendered) {
  *out = {}; *rendered = false;
  SvgInput input;
  JS::RootedObject parent(s.cx);
  JS::RootedObjectVector children(s.cx);
  if (!Read(s, element, &input, &parent, &children)) return false;
  if (!input.node.connected || input.style.Get("display") == "none") return true;
  svg::SvgMatrix matrix;
  unsigned depth = 0;
  bool viewport = false;
  JS::RootedObject ancestor(s.cx, parent);
  while (ancestor) {
    if (++depth > 128) return WebKit::Fail(s.cx, "SVG geometry ancestor budget exceeded");
    SvgInput outer;
    if (!Read(s, ancestor, &outer, &parent, &children)) return false;
    if (outer.style.Get("display") == "none") return true;
    matrix = outer.transform.Then(matrix);
    if (!viewport && outer.node.localName == u"svg") {
      input.viewportWidth = SvgLength(outer, "width", 300, 300);
      input.viewportHeight = SvgLength(outer, "height", 150, 150);
      viewport = true;
    }
    ancestor = parent;
  }
  *rendered = true;
  unsigned visited = 0;
  bool present = false;
  return Visit(s, element, std::move(input), options, matrix, 0, &visited, out, &present);
}
}
