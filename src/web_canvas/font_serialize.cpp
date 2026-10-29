// CanvasRenderingContext2D::SetFont: the CSS `font` shorthand parsed as Servo
// parses it, resolved against the canvas's default "10px sans-serif" parent and
// serialized as Servo_SerializeFontValueForCanvas writes it back. A value Servo
// rejects (or a CSS-wide keyword) leaves the previous font in place.
#include "web_canvas/internal.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace ruyijs::web_canvas::detail {
namespace {
bool Space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
std::string Lower(std::string s) {
  for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  return s;
}

// A small CSS tokenizer: identifiers (with escapes), strings, numbers with
// units or %, commas, slashes and functions (calc is not resolved).
struct Token {
  enum Kind { Ident, String, Number, Percentage, Dimension, Comma, Slash, Function, Other } kind = Other;
  std::string text;  // ident / string value, or the unit (lower-cased)
  double value = 0;
};

bool Hex(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }
void AppendUtf8(uint32_t cp, std::string* out) {
  if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
  if (cp < 0x80) out->push_back(char(cp));
  else if (cp < 0x800) { out->push_back(char(0xC0 | cp >> 6)); out->push_back(char(0x80 | (cp & 63))); }
  else if (cp < 0x10000) {
    out->push_back(char(0xE0 | cp >> 12)); out->push_back(char(0x80 | ((cp >> 6) & 63)));
    out->push_back(char(0x80 | (cp & 63)));
  } else {
    out->push_back(char(0xF0 | cp >> 18)); out->push_back(char(0x80 | ((cp >> 12) & 63)));
    out->push_back(char(0x80 | ((cp >> 6) & 63))); out->push_back(char(0x80 | (cp & 63)));
  }
}
// One escape after the backslash at s[i]; i ends past it.
bool Escape(const std::string& s, size_t& i, std::string* out) {
  ++i;
  if (i >= s.size()) { AppendUtf8(0xFFFD, out); return true; }
  if (s[i] == '\n') return false;
  if (Hex(s[i])) {
    uint32_t cp = 0;
    int n = 0;
    while (i < s.size() && n < 6 && Hex(s[i])) { cp = cp * 16 + uint32_t(std::stoi(std::string(1, s[i]), nullptr, 16)); ++i; ++n; }
    if (i < s.size() && Space(s[i])) ++i;
    AppendUtf8(cp, out);
    return true;
  }
  out->push_back(s[i++]);
  return true;
}
bool NameStart(unsigned char c) { return std::isalpha(c) || c == '_' || c >= 0x80; }
bool NameChar(unsigned char c) { return NameStart(c) || std::isdigit(c) || c == '-'; }
bool StartsIdent(const std::string& s, size_t i) {
  if (i >= s.size()) return false;
  const unsigned char c = s[i];
  if (NameStart(c) || (c == '\\' && i + 1 < s.size() && s[i + 1] != '\n')) return true;
  if (c == '-' && i + 1 < s.size()) {
    const unsigned char d = s[i + 1];
    return NameStart(d) || d == '-' || (d == '\\' && i + 2 < s.size() && s[i + 2] != '\n');
  }
  return false;
}
bool Name(const std::string& s, size_t& i, std::string* out) {
  while (i < s.size()) {
    const unsigned char c = s[i];
    if (NameChar(c)) { out->push_back(char(c)); ++i; }
    else if (c == '\\' && i + 1 < s.size() && s[i + 1] != '\n') { if (!Escape(s, i, out)) return false; }
    else break;
  }
  return true;
}

bool Tokenize(const std::string& s, std::vector<Token>* out) {
  size_t i = 0;
  while (true) {
    while (i < s.size()) {
      if (Space(s[i])) { ++i; continue; }
      if (s.compare(i, 2, "/*") == 0) {
        const size_t e = s.find("*/", i + 2);
        i = e == std::string::npos ? s.size() : e + 2;
        continue;
      }
      break;
    }
    if (i >= s.size()) return true;
    Token t;
    const char c = s[i];
    if (c == ',') { t.kind = Token::Comma; ++i; }
    else if (c == '/') { t.kind = Token::Slash; ++i; }
    else if (c == '"' || c == '\'') {
      ++i;
      t.kind = Token::String;
      bool closed = false;
      while (i < s.size()) {
        if (s[i] == c) { ++i; closed = true; break; }
        if (s[i] == '\n') return false;  // bad string
        if (s[i] == '\\') {
          if (i + 1 < s.size() && s[i + 1] == '\n') { i += 2; continue; }
          if (!Escape(s, i, &t.text)) return false;
          continue;
        }
        t.text.push_back(s[i++]);
      }
      (void)closed;  // an unterminated string ends at EOF
    } else if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '+' ||
               (c == '-' && !StartsIdent(s, i))) {
      const char* begin = s.c_str() + i;
      char* end = nullptr;
      const double v = std::strtod(begin, &end);
      if (end == begin) return false;
      // strtod would read hex and exponents CSS does not have in this form.
      i += size_t(end - begin);
      t.value = v;
      if (i < s.size() && s[i] == '%') { t.kind = Token::Percentage; ++i; }
      else if (StartsIdent(s, i)) {
        t.kind = Token::Dimension;
        std::string unit;
        if (!Name(s, i, &unit)) return false;
        t.text = Lower(unit);
      } else {
        t.kind = Token::Number;
      }
    } else if (StartsIdent(s, i)) {
      std::string name;
      if (!Name(s, i, &name)) return false;
      if (i < s.size() && s[i] == '(') {
        t.kind = Token::Function;
        t.text = Lower(name);
        // Skip to the matching parenthesis.
        int depth = 0;
        for (; i < s.size(); ++i) {
          if (s[i] == '(') ++depth;
          else if (s[i] == ')' && --depth == 0) { ++i; break; }
        }
      } else {
        t.kind = Token::Ident;
        t.text = name;
      }
    } else {
      return false;
    }
    out->push_back(std::move(t));
  }
}

// dtoa_short with 6 significant digits, as Servo writes an f32.
std::string Number(float v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.6g", double(v));
  std::string s = buf;
  if (s.find('e') == std::string::npos && s.find('.') != std::string::npos) {
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
  }
  if (s == "-0") s = "0";
  return s;
}

bool Is(const Token& t, const char* word) { return t.kind == Token::Ident && Lower(t.text) == word; }
bool IsWideKeyword(const std::string& lower) {
  return lower == "initial" || lower == "inherit" || lower == "unset" || lower == "revert" ||
         lower == "revert-layer" || lower == "default";
}
bool IsGeneric(const std::string& lower) {
  return lower == "serif" || lower == "sans-serif" || lower == "monospace" || lower == "cursive" ||
         lower == "fantasy" || lower == "system-ui" || lower == "math";
}

// FamilyName::to_css with the canonical syntax Servo picks for the name.
std::string FamilyText(const std::string& name) {
  bool quote = false, inWord = false;
  for (unsigned char b : name) {
    if (inWord && b == ' ') { inWord = false; continue; }
    if (!inWord && std::isdigit(b)) { quote = true; break; }
    if (std::isalnum(b) || b == '-' || b == '_' || b >= 0x80) { inWord = true; continue; }
    quote = true;
    break;
  }
  if (!quote && !inWord) quote = true;
  const std::string lower = Lower(name);
  if (!quote && (IsWideKeyword(lower) || IsGeneric(lower))) quote = true;
  if (!quote) {
    // serialize_identifier for each word; a word starting with "--" or with an
    // escape-worthy character never reaches here.
    return name;
  }
  std::string out = "\"";
  for (unsigned char c : name) {
    if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
    else if (c < 0x20 || c == 0x7F) {
      char buf[8];
      std::snprintf(buf, sizeof buf, "\\%x ", c);
      out += buf;
    } else out += char(c);
  }
  return out + "\"";
}

const char* kStretch[] = {"ultra-condensed", "extra-condensed", "condensed", "semi-condensed",
                          "semi-expanded", "expanded", "extra-expanded", "ultra-expanded"};
const char* kSystem[] = {"caption", "icon", "menu", "message-box", "small-caption", "status-bar"};
}  // namespace

bool CanonicalCanvasFont(const std::string& input, std::string* out) {
  std::vector<Token> t;
  if (!Tokenize(input, &t) || t.empty()) return false;
  // System font keywords stand alone.
  if (t.size() == 1 && t[0].kind == Token::Ident) {
    const std::string lower = Lower(t[0].text);
    for (const char* name : kSystem)
      if (lower == name) { *out = lower; return true; }
  }
  size_t i = 0;
  std::string style, variant, weight, stretch;
  int normals = 0, count = 0;
  for (; i < t.size() && count < 4; ++count) {
    const Token& k = t[i];
    if (Is(k, "normal")) { ++normals; ++i; continue; }
    if (style.empty() && (Is(k, "italic") || Is(k, "oblique"))) {
      style = Lower(k.text);
      ++i;
      if (style == "oblique" && i < t.size() && t[i].kind == Token::Dimension &&
          (t[i].text == "deg" || t[i].text == "rad" || t[i].text == "grad" || t[i].text == "turn")) {
        double deg = t[i].value;
        if (t[i].text == "rad") deg = deg * 180.0 / 3.14159265358979323846;
        else if (t[i].text == "grad") deg = deg * 0.9;
        else if (t[i].text == "turn") deg = deg * 360.0;
        if (deg < -90 || deg > 90) return false;
        if (float(deg) != 14.0f) style += " " + Number(float(deg)) + "deg";
        ++i;
      }
      continue;
    }
    if (variant.empty() && Is(k, "small-caps")) { variant = "small-caps"; ++i; continue; }
    if (weight.empty() && (Is(k, "bold") || Is(k, "bolder") || Is(k, "lighter"))) {
      weight = Lower(k.text); ++i; continue;
    }
    if (weight.empty() && k.kind == Token::Number) {
      // A number here is a weight only when a size follows.
      if (k.value < 1 || k.value > 1000) return false;
      weight = Number(float(k.value)); ++i; continue;
    }
    if (stretch.empty() && k.kind == Token::Ident) {
      bool matched = false;
      for (const char* name : kStretch)
        if (Lower(k.text) == name) { stretch = name; matched = true; break; }
      if (matched) { ++i; continue; }
    }
    break;
  }
  if (normals + (!style.empty()) + (!variant.empty()) + (!weight.empty()) + (!stretch.empty()) > 4)
    return false;
  // The size, resolved against the 10px parent.
  if (i >= t.size()) return false;
  const Token& s = t[i++];
  double px = -1;
  if (s.kind == Token::Dimension) {
    const std::string& u = s.text;
    if (u == "px") px = s.value;
    else if (u == "em") px = s.value * 10.0;
    else if (u == "rem") px = s.value * 16.0;
    else if (u == "pt") px = s.value * 96.0 / 72.0;
    else if (u == "pc") px = s.value * 16.0;
    else if (u == "in") px = s.value * 96.0;
    else if (u == "cm") px = s.value * 96.0 / 2.54;
    else if (u == "mm") px = s.value * 96.0 / 25.4;
    else if (u == "q") px = s.value * 96.0 / 101.6;
    else return false;
    if (s.value < 0) return false;
  } else if (s.kind == Token::Percentage) {
    if (s.value < 0) return false;
    px = s.value / 100.0 * 10.0;
  } else if (s.kind == Token::Number && s.value == 0) {
    px = 0;
  } else if (s.kind == Token::Ident) {
    static const struct { const char* name; double px; } kSizes[] = {
        {"xx-small", 9}, {"x-small", 10}, {"small", 13}, {"medium", 16}, {"large", 18},
        {"x-large", 24}, {"xx-large", 32}, {"xxx-large", 48}, {"larger", 12}, {"smaller", 10 / 1.2}};
    const std::string lower = Lower(s.text);
    for (const auto& k : kSizes) if (lower == k.name) px = k.px;
    if (px < 0) return false;
  } else {
    return false;  // calc() and other functions are not resolved here
  }
  // A computed font-size is in app units.
  px = std::floor(px * 60.0 + 0.5) / 60.0;
  // Optional "/ line-height", which canvas drops.
  if (i < t.size() && t[i].kind == Token::Slash) {
    ++i;
    if (i >= t.size()) return false;
    const Token& lh = t[i++];
    const bool ok = Is(lh, "normal") || lh.kind == Token::Number || lh.kind == Token::Percentage ||
                    lh.kind == Token::Dimension;
    if (!ok || ((lh.kind != Token::Ident) && lh.value < 0)) return false;
  }
  // The family list.
  std::vector<std::string> families;
  while (true) {
    if (i >= t.size()) return false;
    if (t[i].kind == Token::String) {
      families.push_back(FamilyText(t[i].text));
      ++i;
    } else if (t[i].kind == Token::Ident) {
      const std::string first = Lower(t[i].text);
      if (IsGeneric(first) && (i + 1 >= t.size() || t[i + 1].kind == Token::Comma)) {
        families.push_back(first);
        ++i;
      } else {
        std::string name = t[i].text;
        ++i;
        while (i < t.size() && t[i].kind == Token::Ident) name += " " + t[i++].text;
        if (IsWideKeyword(Lower(name))) return false;
        families.push_back(FamilyText(name));
      }
    } else {
      return false;
    }
    if (i >= t.size()) break;
    if (t[i].kind != Token::Comma) return false;
    ++i;
  }
  std::string result;
  for (const std::string* part : {&style, &variant, &weight, &stretch})
    if (!part->empty()) result += *part + " ";
  result += Number(float(px)) + "px ";
  for (size_t k = 0; k < families.size(); ++k) result += (k ? ", " : "") + families[k];
  *out = result;
  return true;
}
}  // namespace ruyijs::web_canvas::detail
