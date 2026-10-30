// Servo specified-value serialization (see css_specified_value.h). Each rule
// below names the Servo serializer it follows:
//   numbers      dtoa_short on f32 (CssFloatText); <integer> tokens as written
//   colors       cssparser: hex and legacy rgb/hsl/hwb -> rgb()/rgba(); named
//                colors, currentcolor and transparent stay keywords
//   strings/url  serialize_string: double quotes; url(x) -> url("x")
//   lengths      a unitless 0 (and an SVG unitless length) is a px length
//   transform    GenericTransformOperation (skip_if / contextual_skip_if)
//   shorthands   properties/shorthands.rs to_css of animation, transition,
//                outline; box-shadow/text-shadow field order; grid-area/row/
//                column; TransformOrigin (all three components)
#include "domain/styling/css_specified_value.h"
#include "domain/styling/css_color_value.h"
#include "domain/styling/css_float_text.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace ruyijs::domain::styling {
// Named, not anonymous: this file is compiled inside the styling unity unit.
namespace specified_value {
struct PropertyName { const char* idl; const char* css; const char* attribute; };
constexpr PropertyName kProperties[] = {
#include "document_model/css_properties.inc"
};

std::string SvLower(std::string_view text) {
  std::string out(text);
  for (auto& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
  return out;
}
bool SvSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

// One top-level component and the separator written before it.
struct Item { std::string text; char sep = 0; };

// Splits a value at depth 0 into components separated by whitespace, commas
// or slashes; functions, parentheses and strings stay one component.
std::vector<Item> Split(std::string_view value) {
  std::vector<Item> items;
  std::string current;
  char pending = 0;
  int depth = 0;
  auto flush = [&] {
    if (current.empty()) return;
    items.push_back({current, items.empty() ? char(0) : (pending ? pending : ' ')});
    current.clear(); pending = 0;
  };
  for (size_t i = 0; i < value.size(); ++i) {
    const char c = value[i];
    if (c == '"' || c == '\'') {
      const size_t start = i++;
      while (i < value.size() && value[i] != c) { if (value[i] == '\\') ++i; ++i; }
      current.append(value.substr(start, std::min(i, value.size() - 1) - start + 1));
      continue;
    }
    if (c == '(') ++depth;
    if (c == ')' && depth > 0) --depth;
    if (depth == 0 && (SvSpace(c) || c == ',' || c == '/')) {
      flush();
      if (c == ',' || c == '/') pending = c;
      continue;
    }
    current.push_back(c);
  }
  flush();
  return items;
}
std::string Join(const std::vector<Item>& items) {
  std::string out;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i) out += items[i].sep == ',' ? ", " : items[i].sep == '/' ? " / " : " ";
    out += items[i].text;
  }
  return out;
}
// Splits a comma-separated list at depth 0 into its members' components.
std::vector<std::vector<Item>> Groups(const std::vector<Item>& items) {
  std::vector<std::vector<Item>> groups(1);
  for (const auto& item : items) {
    if (item.sep == ',' && !groups.back().empty()) groups.emplace_back();
    groups.back().push_back(item);
    groups.back().back().sep = groups.back().size() == 1 ? char(0) : item.sep;
  }
  return groups;
}
std::string JoinGroups(const std::vector<std::string>& groups) {
  std::string out;
  for (const auto& g : groups) out += (out.empty() ? "" : ", ") + g;
  return out;
}

// A numeric token: optional sign, digits, optional fraction and exponent,
// optional unit or %. `integer` marks a token written without '.'/exponent.
struct Numeric { bool ok = false; double value = 0; std::string unit; bool integer = false; };
Numeric ParseNumeric(std::string_view text) {
  Numeric n;
  size_t i = 0;
  if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
  const size_t digits = i;
  while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
  bool fraction = false, exponent = false;
  if (i < text.size() && text[i] == '.' && i + 1 < text.size() &&
      std::isdigit(static_cast<unsigned char>(text[i + 1]))) {
    fraction = true; ++i;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
  }
  if (i == digits) return n;
  if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
    size_t j = i + 1;
    if (j < text.size() && (text[j] == '+' || text[j] == '-')) ++j;
    if (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) {
      exponent = true; i = j;
      while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
    }
  }
  const std::string number(text.substr(0, i));
  n.unit = SvLower(text.substr(i));
  if (!n.unit.empty() && n.unit != "%")
    for (char c : n.unit) if (!std::isalpha(static_cast<unsigned char>(c))) return n;
  n.value = std::strtod(number.c_str(), nullptr);
  n.integer = !fraction && !exponent;
  n.ok = true;
  return n;
}
std::string NumberText(const Numeric& n) {
  // An <integer> token reads exactly (Servo i32, clamped); a <number> as f32.
  if (n.integer && std::fabs(n.value) <= 2147483647.0) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.0f", n.value);
    return std::string(buffer) == "-0" ? "0" : buffer;
  }
  return CssFloatText(float(n.value));
}

bool HexColor(std::string_view t) {
  if (t.size() < 2 || t[0] != '#') return false;
  const size_t len = t.size() - 1;
  if (len != 3 && len != 4 && len != 6 && len != 8) return false;
  for (size_t i = 1; i < t.size(); ++i) if (!std::isxdigit(static_cast<unsigned char>(t[i]))) return false;
  return true;
}
std::string FunctionName(std::string_view t) {
  const auto open = t.find('(');
  if (open == std::string_view::npos || open == 0 || t.back() != ')') return {};
  return SvLower(t.substr(0, open));
}
std::string FunctionArgs(std::string_view t) {
  const auto open = t.find('(');
  return std::string(t.substr(open + 1, t.size() - open - 2));
}
bool LegacyColorFunction(const std::string& f) {
  return f == "rgb" || f == "rgba" || f == "hsl" || f == "hsla" || f == "hwb";
}
bool ColorComponent(std::string_view t) {
  if (HexColor(t)) return true;
  const auto f = FunctionName(t);
  if (!f.empty()) return LegacyColorFunction(f) || f == "lab" || f == "lch" || f == "oklab" ||
      f == "oklch" || f == "color" || f == "color-mix" || f == "light-dark";
  const auto lower = SvLower(t);
  return lower == "currentcolor" || lower == "transparent" || CssColorNameKnown(lower);
}
std::string StringText(std::string_view t) {
  // serialize_string: the decoded string in double quotes, escaping " and \.
  std::string body;
  for (size_t i = 1; i + 1 < t.size(); ++i) {
    if (t[i] == '\\' && i + 2 < t.size()) { body.push_back('\\'); body.push_back(t[++i]); continue; }
    if (t[i] == '"') body += "\\\"";
    else body.push_back(t[i]);
  }
  return "\"" + body + "\"";
}

enum class Zero { Keep, Px, Deg };
// keepCase: the property carries <custom-ident>s (case-sensitive); any other
// identifier is a keyword, which Servo serializes in lowercase.
struct Context { Zero zero = Zero::Keep; bool svgPx = false; bool keepCase = false; };

std::string Component(std::string_view t, const Context& ctx);
std::string Arguments(std::string_view args, const Context& ctx) {
  auto items = Split(args);
  for (auto& item : items) item.text = Component(item.text, ctx);
  return Join(items);
}
std::string Component(std::string_view t, const Context& ctx) {
  if (t.empty()) return {};
  if (t.front() == '"' || t.front() == '\'') return t.size() >= 2 ? StringText(t) : std::string(t);
  if (HexColor(t)) { auto c = CssComputedColor(t, {}); return c.empty() ? std::string(t) : c; }
  const auto f = FunctionName(t);
  if (!f.empty()) {
    if (LegacyColorFunction(f)) { auto c = CssComputedColor(t, {}); if (!c.empty()) return c; }
    if (f == "url") {
      auto inner = FunctionArgs(t);
      size_t a = 0, b = inner.size();
      while (a < b && SvSpace(inner[a])) ++a;
      while (b > a && SvSpace(inner[b - 1])) --b;
      inner = inner.substr(a, b - a);
      if (!inner.empty() && (inner.front() == '"' || inner.front() == '\'')) return "url(" + StringText(inner) + ")";
      return "url(" + StringText("\"" + inner + "\"") + ")";
    }
    // Math functions keep their authored text (Servo simplifies them; out of
    // scope here) as do functions whose arguments are not plain components.
    if (f == "calc" || f == "min" || f == "max" || f == "clamp" || f == "var" || f == "env" ||
        f == "attr" || f.starts_with("-")) return std::string(t);
    const std::string name = ctx.keepCase ? std::string(t.substr(0, f.size())) : f;
    return name + "(" + Arguments(FunctionArgs(t), ctx) + ")";
  }
  const auto n = ParseNumeric(t);
  if (!n.ok) {
    if (!ctx.keepCase && t.front() != '#' && t.front() != '[') return SvLower(t);
    if (ctx.keepCase && ColorComponent(t)) return SvLower(t);
    return std::string(t);
  }
  std::string text = NumberText(n);
  if (n.unit.empty()) {
    if (ctx.svgPx) return text + "px";
    if (n.value == 0 && ctx.zero == Zero::Px) return "0px";
    if (n.value == 0 && ctx.zero == Zero::Deg) return "0deg";
    return text;
  }
  return text + n.unit;
}
std::string Generic(std::string_view value, const Context& ctx) {
  auto items = Split(value);
  for (auto& item : items) item.text = Component(item.text, ctx);
  return Join(items);
}

// ---- property classes -------------------------------------------------------
bool LengthProperty(std::string_view n) {
  auto starts = [&](std::string_view p) { return n.substr(0, p.size()) == p; };
  auto ends = [&](std::string_view s) { return n.size() >= s.size() && n.substr(n.size() - s.size()) == s; };
  if (starts("border-image")) return false;
  if (starts("margin") || starts("padding") || starts("inset") || starts("scroll-margin") ||
      starts("scroll-padding")) return true;
  if (starts("border") && (ends("-width") || ends("radius") || n == "border-spacing")) return true;
  static constexpr std::string_view kNames[] = {
      "top", "right", "bottom", "left", "width", "height", "min-width", "min-height", "max-width",
      "max-height", "block-size", "inline-size", "min-block-size", "min-inline-size",
      "max-block-size", "max-inline-size", "outline-width", "outline-offset", "gap", "row-gap",
      "column-gap", "grid-gap", "grid-row-gap", "grid-column-gap", "letter-spacing",
      "word-spacing", "text-indent", "font-size", "flex-basis", "background-position",
      "background-position-x", "background-position-y", "background-size", "mask-position",
      "mask-size", "perspective-origin", "perspective", "translate", "column-width",
      "column-rule-width", "vertical-align", "text-underline-offset", "text-decoration-thickness",
      "shape-margin", "offset-distance", "-webkit-text-stroke-width", "grid-template-columns",
      "grid-template-rows", "grid-auto-columns", "grid-auto-rows", "clip-path", "shape-outside",
      "x", "y", "cx", "cy", "r",
      "rx", "ry"};
  return std::find(std::begin(kNames), std::end(kNames), n) != std::end(kNames);
}
bool SvgLengthProperty(std::string_view n) {
  return n == "stroke-width" || n == "stroke-dashoffset" || n == "stroke-dasharray";
}

// ---- transform --------------------------------------------------------------
std::string TransformFunction(std::string_view t) {
  const auto f = FunctionName(t);
  if (f.empty()) return Component(t, {});
  static constexpr std::pair<std::string_view, std::string_view> kCanonical[] = {
      {"translatex", "translateX"}, {"translatey", "translateY"}, {"translatez", "translateZ"},
      {"translate3d", "translate3d"}, {"translate", "translate"}, {"scalex", "scaleX"},
      {"scaley", "scaleY"}, {"scalez", "scaleZ"}, {"scale3d", "scale3d"}, {"scale", "scale"},
      {"rotatex", "rotateX"}, {"rotatey", "rotateY"}, {"rotatez", "rotateZ"},
      {"rotate3d", "rotate3d"}, {"rotate", "rotate"}, {"skewx", "skewX"}, {"skewy", "skewY"},
      {"skew", "skew"}, {"matrix3d", "matrix3d"}, {"matrix", "matrix"},
      {"perspective", "perspective"}};
  std::string name;
  for (const auto& [lower, canonical] : kCanonical) if (f == lower) name = canonical;
  if (name.empty()) return std::string(t);
  auto args = Split(FunctionArgs(t));
  std::vector<std::string> values;
  for (size_t i = 0; i < args.size(); ++i) {
    Context ctx;
    if (f.starts_with("translate") || f == "perspective") ctx.zero = Zero::Px;
    else if ((f.starts_with("rotate") && f != "rotate3d") || f.starts_with("skew") ||
             (f == "rotate3d" && i == 3)) ctx.zero = Zero::Deg;
    values.push_back(Component(args[i].text, ctx));
  }
  auto zero = [](const std::string& v) {
    const auto n = ParseNumeric(v);
    return n.ok && n.value == 0 && n.unit != "%";
  };
  if ((f == "translate" || f == "skew") && values.size() == 2 && zero(values[1])) values.pop_back();
  if (f == "scale" && values.size() == 2 && values[1] == values[0]) values.pop_back();
  std::string out = name + "(";
  for (size_t i = 0; i < values.size(); ++i) out += (i ? ", " : "") + values[i];
  return out + ")";
}
std::string Transform(std::string_view value) {
  auto items = Split(value);
  for (auto& item : items) item.text = TransformFunction(item.text);
  return Join(items);
}

// ---- shadows ----------------------------------------------------------------
std::string Shadows(std::string_view value) {
  const auto items = Split(value);
  if (items.size() == 1 && SvLower(items[0].text) == "none") return "none";
  std::vector<std::string> out;
  for (const auto& group : Groups(items)) {
    std::string color, lengths;
    bool inset = false;
    for (const auto& item : group) {
      if (SvLower(item.text) == "inset") inset = true;
      else if (ColorComponent(item.text)) color = Component(item.text, {});
      else lengths += (lengths.empty() ? "" : " ") + Component(item.text, {Zero::Px});
    }
    std::string one = color;
    if (!lengths.empty()) one += (one.empty() ? "" : " ") + lengths;
    if (inset) one += (one.empty() ? "" : " ") + std::string("inset");
    out.push_back(one);
  }
  return JoinGroups(out);
}

// ---- outline ----------------------------------------------------------------
bool LineStyle(std::string_view t) {
  static constexpr std::string_view kStyles[] = {"none", "hidden", "dotted", "dashed", "solid",
                                                "double", "groove", "ridge", "inset", "outset", "auto"};
  return std::find(std::begin(kStyles), std::end(kStyles), t) != std::end(kStyles);
}
std::string Outline(std::string_view value) {
  std::string color, style, width;
  for (const auto& item : Split(value)) {
    const auto lower = SvLower(item.text);
    if (LineStyle(lower)) style = lower;
    else if (ColorComponent(item.text)) color = Component(item.text, {});
    else width = Component(item.text, {Zero::Px});
  }
  std::string out;
  auto add = [&](const std::string& v) { if (!v.empty()) out += (out.empty() ? "" : " ") + v; };
  if (color != "currentcolor" && color != "auto") add(color);
  if (style != "none") add(style);
  if (width != "medium") add(width);
  if (out.empty()) out = style.empty() ? "none" : style;
  return out;
}

// ---- animation / transition -------------------------------------------------
bool TimeToken(std::string_view t) {
  const auto n = ParseNumeric(t);
  return n.ok && (n.unit == "s" || n.unit == "ms");
}
bool TimingKeyword(std::string_view t) {
  return t == "ease" || t == "linear" || t == "ease-in" || t == "ease-out" || t == "ease-in-out" ||
         t == "step-start" || t == "step-end";
}
bool TimingFunction(std::string_view t) {
  const auto f = FunctionName(t);
  return TimingKeyword(SvLower(t)) || f == "cubic-bezier" || f == "steps" || f == "linear";
}
bool ZeroTime(std::string_view t) { const auto n = ParseNumeric(t); return n.ok && n.value == 0; }
std::string Animation(std::string_view value) {
  std::vector<std::string> out;
  for (const auto& group : Groups(Split(value))) {
    std::string duration, timing, delay, count, direction, fill, play, name;
    for (const auto& item : group) {
      const auto lower = SvLower(item.text);
      if (TimeToken(item.text) && duration.empty()) duration = Component(item.text, {});
      else if (TimeToken(item.text) && delay.empty()) delay = Component(item.text, {});
      else if (TimingFunction(item.text) && timing.empty()) timing = Component(item.text, {});
      else if ((lower == "infinite" || ParseNumeric(item.text).ok) && count.empty()) count = Component(lower, {});
      else if ((lower == "normal" || lower == "reverse" || lower == "alternate" ||
                lower == "alternate-reverse") && direction.empty()) direction = lower;
      else if ((lower == "none" || lower == "forwards" || lower == "backwards" || lower == "both") &&
               fill.empty()) fill = lower;
      else if ((lower == "running" || lower == "paused") && play.empty()) play = lower;
      else if (name.empty()) { Context keep; keep.keepCase = true; name = Component(item.text, keep); }
    }
    if (name.empty()) name = "none";
    const bool hasDuration = !duration.empty() && duration != "auto" && !ZeroTime(duration);
    const bool hasTiming = !timing.empty() && timing != "ease";
    const bool hasDelay = !delay.empty() && !ZeroTime(delay);
    const bool hasCount = !count.empty() && count != "1";
    const bool hasDirection = !direction.empty() && direction != "normal";
    const bool hasFill = !fill.empty() && fill != "none";
    const bool hasPlay = !play.empty() && play != "running";
    const auto lowerName = SvLower(name);
    std::string one;
    auto add = [&](const std::string& v) { one += (one.empty() ? "" : " ") + v; };
    if (hasDuration || hasDelay) add(duration.empty() ? "0s" : duration);
    if (hasTiming || TimingKeyword(lowerName)) add(timing.empty() ? "ease" : timing);
    if (hasDelay) add(delay);
    if (hasCount) add(count);
    if (hasDirection || lowerName == "normal" || lowerName == "reverse" || lowerName == "alternate" ||
        lowerName == "alternate-reverse") add(direction.empty() ? "normal" : direction);
    if (hasFill || lowerName == "forwards" || lowerName == "backwards" || lowerName == "both")
      add(fill.empty() ? "none" : fill);
    if (hasPlay || lowerName == "running" || lowerName == "paused") add(play.empty() ? "running" : play);
    if (lowerName != "none" || one.empty()) add(name);
    out.push_back(one);
  }
  return JoinGroups(out);
}
std::string Transition(std::string_view value) {
  std::vector<std::string> out;
  for (const auto& group : Groups(Split(value))) {
    std::string property, duration, timing, delay, behavior;
    for (const auto& item : group) {
      const auto lower = SvLower(item.text);
      if (TimeToken(item.text) && duration.empty()) duration = Component(item.text, {});
      else if (TimeToken(item.text) && delay.empty()) delay = Component(item.text, {});
      else if (TimingFunction(item.text) && timing.empty()) timing = Component(item.text, {});
      else if ((lower == "normal" || lower == "allow-discrete") && behavior.empty()) behavior = lower;
      else if (property.empty()) property = lower;
    }
    if (property.empty()) property = "all";
    const bool hasDuration = !duration.empty() && !ZeroTime(duration);
    const bool hasTiming = !timing.empty() && timing != "ease";
    const bool hasDelay = !delay.empty() && !ZeroTime(delay);
    const bool hasBehavior = !behavior.empty() && behavior != "normal";
    const bool any = hasDuration || hasTiming || hasDelay || hasBehavior;
    std::string one;
    auto add = [&](const std::string& v) { one += (one.empty() ? "" : " ") + v; };
    if (property != "all" || !any) add(property);
    if (hasDuration || hasDelay) add(duration.empty() ? "0s" : duration);
    if (hasTiming) add(timing);
    if (hasDelay) add(delay);
    if (hasBehavior) add(behavior);
    out.push_back(one);
  }
  return JoinGroups(out);
}

// ---- font-family ------------------------------------------------------------
bool Identifier(std::string_view w) {
  if (w.empty()) return false;
  size_t i = 0;
  auto start = [](unsigned char c) { return std::isalpha(c) || c == '_' || c >= 0x80; };
  if (w[0] == '-') { if (w.size() == 1) return false; i = 1; if (w[1] == '-') i = 2; else if (!start(w[1])) return false; }
  else if (!start(static_cast<unsigned char>(w[0]))) return false;
  for (; i < w.size(); ++i) {
    const unsigned char c = w[i];
    if (!(std::isalnum(c) || c == '_' || c == '-' || c >= 0x80)) return false;
  }
  return true;
}
bool ReservedFamilyWord(std::string_view lower) {
  static constexpr std::string_view kWords[] = {
      "serif", "sans-serif", "monospace", "cursive", "fantasy", "system-ui", "math", "emoji",
      "fangsong", "ui-serif", "ui-sans-serif", "ui-monospace", "ui-rounded", "-moz-fixed",
      "inherit", "initial", "unset", "revert", "revert-layer", "default"};
  return std::find(std::begin(kWords), std::end(kWords), lower) != std::end(kWords);
}
std::string FontFamily(std::string_view value) {
  std::vector<std::string> out;
  for (const auto& group : Groups(Split(value))) {
    std::string family;
    if (group.size() == 1 && (group[0].text.front() == '"' || group[0].text.front() == '\'')) {
      const auto& quoted = group[0].text;
      const std::string body = quoted.size() >= 2 ? quoted.substr(1, quoted.size() - 2) : std::string();
      // FamilyName::to_css: a quoted name whose words are all identifiers,
      // none of them a generic or CSS-wide keyword, reads unquoted.
      std::vector<std::string> words;
      std::string word;
      bool plain = body.find('\\') == std::string::npos && !body.empty() &&
                   !SvSpace(body.front()) && !SvSpace(body.back());
      for (char c : body) {
        if (c == ' ') { if (word.empty()) plain = false; words.push_back(word); word.clear(); }
        else word.push_back(c);
      }
      words.push_back(word);
      for (const auto& w : words) if (!Identifier(w) || ReservedFamilyWord(SvLower(w))) plain = false;
      family = plain ? body : StringText(quoted);
    } else {
      for (const auto& item : group) family += (family.empty() ? "" : " ") + item.text;
    }
    out.push_back(family);
  }
  return JoinGroups(out);
}

// ---- grid placement ---------------------------------------------------------
bool CustomIdent(std::string_view t) {
  const auto lower = SvLower(t);
  return Identifier(t) && lower != "auto" && lower != "span";
}
std::string GridLine(std::string_view value) {
  auto items = Split(value);
  Context ctx; ctx.keepCase = true;
  for (auto& item : items) item.text = SvLower(item.text) == "span" || SvLower(item.text) == "auto"
      ? SvLower(item.text) : Component(item.text, ctx);
  return Join(items);
}
std::string GridPlacement(std::string_view value, size_t count) {
  std::vector<std::string> parts;
  std::string current;
  int depth = 0;
  for (char c : value) {
    if (c == '(') ++depth;
    if (c == ')') --depth;
    if (c == '/' && depth == 0) { parts.push_back(GridLine(current)); current.clear(); continue; }
    current.push_back(c);
  }
  parts.push_back(GridLine(current));
  if (parts.size() > count) return std::string(value);
  // A missing line is its start line when that is a <custom-ident>, else auto
  // (grid-area: column-start from row-start; row-end from row-start;
  // column-end from column-start).
  auto derived = [](const std::string& from) { return CustomIdent(from) ? from : std::string("auto"); };
  if (count == 4) {
    if (parts.size() < 2) parts.push_back(derived(parts[0]));
    if (parts.size() < 3) parts.push_back(derived(parts[0]));
    if (parts.size() < 4) parts.push_back(derived(parts[1]));
  } else if (parts.size() < 2) parts.push_back(derived(parts[0]));
  auto omittable = [](const std::string& value, const std::string& start) {
    return CustomIdent(start) ? value == start : value == "auto";
  };
  size_t keep = count;
  if (count == 4) {
    if (omittable(parts[3], parts[1])) {
      keep = 3;
      if (omittable(parts[2], parts[0])) { keep = 2; if (omittable(parts[1], parts[0])) keep = 1; }
    }
  } else if (omittable(parts[1], parts[0])) keep = 1;
  std::string out;
  for (size_t i = 0; i < keep; ++i) out += (i ? " / " : "") + parts[i];
  return out;
}

// ---- transform-origin -------------------------------------------------------
std::string TransformOrigin(std::string_view value) {
  const auto items = Split(value);
  if (items.empty() || items.size() > 3) return Generic(value, {Zero::Px});
  std::string x, y, z = "0px";
  std::vector<std::string> rest;
  for (const auto& item : items) rest.push_back(Component(item.text, {Zero::Px}));
  auto horizontal = [](const std::string& v) { return v == "left" || v == "right"; };
  auto vertical = [](const std::string& v) { return v == "top" || v == "bottom"; };
  if (rest.size() == 1) {
    if (vertical(rest[0])) { x = "center"; y = rest[0]; }
    else { x = rest[0]; y = "center"; }
  } else {
    x = rest[0]; y = rest[1];
    if (vertical(x) || horizontal(y)) std::swap(x, y);
    if (x == "center" && horizontal(y)) std::swap(x, y);
    if (y == "center" && vertical(x)) std::swap(x, y);
    if (rest.size() == 3) z = rest[2];
  }
  return x + " " + y + " " + z;
}

std::string DashArray(std::string_view value) {
  const auto items = Split(value);
  if (items.size() == 1 && SvLower(items[0].text) == "none") return "none";
  std::string out;
  for (const auto& item : items) out += (out.empty() ? "" : ", ") + Component(item.text, {Zero::Px, true});
  return out;
}

// filter / backdrop-filter: blur() takes a length; drop-shadow() is a shadow.
std::string Filter(std::string_view value) {
  auto items = Split(value);
  for (auto& item : items) {
    const auto f = FunctionName(item.text);
    if (f == "blur") item.text = "blur(" + Arguments(FunctionArgs(item.text), {Zero::Px}) + ")";
    else if (f == "drop-shadow") item.text = "drop-shadow(" + Shadows(FunctionArgs(item.text)) + ")";
    else item.text = Component(item.text, {});
  }
  return Join(items);
}
// The background shorthand writes its color after the final layer's other
// components (shorthands.rs background::to_css longhand order).
std::string Background(std::string_view value) {
  auto items = Split(Generic(value, {Zero::Px}));
  size_t layer = 0;
  for (size_t i = 0; i < items.size(); ++i) if (items[i].sep == ',') layer = i;
  for (size_t i = layer; i < items.size(); ++i) {
    if (!ColorComponent(items[i].text) || i + 1 == items.size()) continue;
    Item color = items[i];
    items.erase(items.begin() + i);
    if (i == layer && i < items.size()) { items[i].sep = color.sep; }
    color.sep = ' ';
    items.push_back(color);
    break;
  }
  return Join(items);
}
bool CustomIdentProperty(std::string_view n) {
  auto starts = [&](std::string_view p) { return n.substr(0, p.size()) == p; };
  return starts("animation") || starts("grid") || starts("counter") || starts("list-style") ||
         starts("view-transition") || starts("container") || starts("anchor") || starts("position-") ||
         starts("transition") || n == "will-change" || n == "content" || n == "quotes" || n == "page" ||
         n == "font-palette" || n == "font-feature-settings" || n == "font-variation-settings" ||
         n == "font-language-override" || n == "-moz-binding" || n == "font-family" || n == "font";
}

bool WideKeyword(std::string_view lower) {
  return lower == "inherit" || lower == "initial" || lower == "unset" || lower == "revert" ||
         lower == "revert-layer";
}
bool HasReference(std::string_view value) {
  const auto lower = SvLower(value);
  return lower.find("var(") != std::string::npos || lower.find("env(") != std::string::npos ||
         lower.find("attr(") != std::string::npos;
}
}  // namespace specified_value

namespace specified_value {
// Accessor (dash-case or IDL) name -> the property it stands for, built once.
const std::unordered_map<std::string_view, std::string_view>& PropertyIndex() {
  static const auto* index = [] {
    auto* map = new std::unordered_map<std::string_view, std::string_view>();
    for (const auto& property : kProperties) {
      map->emplace(property.idl, property.css);
      map->emplace(property.css, property.css);
    }
    return map;
  }();
  return *index;
}
}  // namespace specified_value

bool CssKnownProperty(std::string_view name) {
  if (name.size() > 2 && name.substr(0, 2) == "--") return true;
  return specified_value::PropertyIndex().count(name) != 0;
}
std::string CssPropertyAliasTarget(std::string_view name) {
  const auto& index = specified_value::PropertyIndex();
  const auto found = index.find(name);
  if (found == index.end() || found->second == name) return {};
  return std::string(found->second);
}

std::string CssSpecifiedValue(std::string_view name, std::string_view value) {
  using namespace specified_value;
  if (name.substr(0, 2) == "--" || value.empty() || HasReference(value)) return std::string(value);
  const auto lower = SvLower(value);
  if (WideKeyword(lower)) return lower;
  if (name == "transform") return Transform(value);
  if (name == "box-shadow" || name == "text-shadow" || name == "-webkit-box-shadow") return Shadows(value);
  if (name == "outline") return Outline(value);
  if (name == "animation" || name == "-webkit-animation") return Animation(value);
  if (name == "transition" || name == "-webkit-transition") return Transition(value);
  if (name == "font-family") return FontFamily(value);
  if (name == "grid-area") return GridPlacement(value, 4);
  if (name == "grid-row" || name == "grid-column") return GridPlacement(value, 2);
  if (name == "transform-origin" || name == "-webkit-transform-origin") return TransformOrigin(value);
  if (name == "stroke-dasharray") return DashArray(value);
  if (name == "filter" || name == "backdrop-filter") return Filter(value);
  if (name == "background") return Background(value);
  if (name == "opacity" || name == "fill-opacity" || name == "stroke-opacity" ||
      name == "flood-opacity" || name == "stop-opacity") {
    // <opacity-value>: a percentage is stored as the number it stands for.
    const auto n = ParseNumeric(value);
    if (n.ok && n.unit == "%") { Numeric number; number.ok = true; number.value = n.value / 100; return NumberText(number); }
  }
  Context ctx;
  ctx.keepCase = CustomIdentProperty(name);
  if (SvgLengthProperty(name)) ctx = {Zero::Px, true};
  else if (LengthProperty(name)) ctx.zero = Zero::Px;
  return Generic(value, ctx);
}
}  // namespace ruyijs::domain::styling
