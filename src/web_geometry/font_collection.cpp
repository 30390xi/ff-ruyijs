#ifndef _WIN32
// System faces for the shared shaper. CSS generics name Mac families. A named
// family that Core Text silently replaces is rejected, so an unknown family
// falls through to CTFontCreateForString instead of measuring the fallback.
#include "web_geometry/font_backend.h"
#include "web_geometry/font_config.h"

#include <CoreText/CoreText.h>

#include <cmath>
#include <string>

namespace ruyijs::web_geometry {
namespace {
std::string Utf8(const std::wstring& text) {
  std::string out;
  for (wchar_t unit : text) {
    const uint32_t code = uint32_t(unit);
    if (code < 0x80) {
      out.push_back(char(code));
    } else if (code < 0x800) {
      out.push_back(char(0xC0 | (code >> 6)));
      out.push_back(char(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
      out.push_back(char(0xE0 | (code >> 12)));
      out.push_back(char(0x80 | ((code >> 6) & 0x3F)));
      out.push_back(char(0x80 | (code & 0x3F)));
    } else {
      out.push_back(char(0xF0 | (code >> 18)));
      out.push_back(char(0x80 | ((code >> 12) & 0x3F)));
      out.push_back(char(0x80 | ((code >> 6) & 0x3F)));
      out.push_back(char(0x80 | (code & 0x3F)));
    }
  }
  return out;
}
std::wstring LowerAscii(std::wstring text) {
  for (wchar_t& ch : text)
    if (ch >= L'A' && ch <= L'Z') ch = wchar_t(ch - L'A' + L'a');
  return text;
}
bool IsGeneric(const std::wstring& name) {
  const auto key = LowerAscii(name);
  return key == L"sans-serif" || key == L"serif" || key == L"monospace" || key == L"system-ui" ||
         key == L"-apple-system" || key == L"ui-monospace" || key == L"cursive" ||
         key == L"fantasy";
}
std::wstring GenericFamily(const std::wstring& name) {
  const auto key = LowerAscii(name);
  if (key == L"sans-serif" || key == L"system-ui" || key == L"-apple-system")
    return L".AppleSystemUIFont";
  if (key == L"serif") return L"Times New Roman";
  if (key == L"monospace" || key == L"ui-monospace") return L"Menlo";
  if (key == L"cursive") return L"Apple Chancery";
  if (key == L"fantasy") return L"Papyrus";
  return name;
}
CFStringRef MakeString(const std::wstring& text) {
  const std::string utf8 = Utf8(text);
  return CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(utf8.data()),
                                 CFIndex(utf8.size()), kCFStringEncodingUTF8, false);
}
bool FamilyMatches(CTFontRef font, const std::wstring& expected) {
  CFStringRef want = MakeString(expected);
  CFStringRef got = font ? CTFontCopyFamilyName(font) : nullptr;
  const bool match = want && got && CFStringCompare(want, got, kCFCompareCaseInsensitive) == 0;
  if (want) CFRelease(want);
  if (got) CFRelease(got);
  return match;
}
bool LastResort(CTFontRef font) {
  CFStringRef got = font ? CTFontCopyFamilyName(font) : nullptr;
  const bool match = got && CFStringCompare(got, CFSTR("LastResort"), 0) == 0;
  if (got) CFRelease(got);
  return match;
}
CTFontRef FontAtEm(CTFontRef font) {
  if (!font) return nullptr;
  const CGFloat em = CTFontGetUnitsPerEm(font);
  if (!(em > 0)) return nullptr;
  return CTFontCreateCopyWithAttributes(font, em, nullptr, nullptr);
}
CTFontRef MakeFont(const std::wstring& requested, unsigned weight, bool italic, bool* synthetic) {
  *synthetic = false;
  const bool generic = IsGeneric(requested);
  const std::wstring resolved = generic ? GenericFamily(requested) : requested;
  CFStringRef name = MakeString(resolved);
  if (!name) return nullptr;
  CTFontRef named = CTFontCreateWithName(name, 0, nullptr);
  CFRelease(name);
  if (!named) return nullptr;
  if (!generic && !FamilyMatches(named, resolved)) {
    CFRelease(named);
    return nullptr;
  }
  CTFontSymbolicTraits want = 0;
  if (weight >= 600) want |= kCTFontBoldTrait;
  if (italic) want |= kCTFontItalicTrait;
  CTFontRef styled = named;
  if (want) {
    CTFontRef copied = CTFontCreateCopyWithSymbolicTraits(named, 0, nullptr, want, want);
    if (!copied && italic && (want & kCTFontBoldTrait))
      copied = CTFontCreateCopyWithSymbolicTraits(named, 0, nullptr, kCTFontItalicTrait,
                                                  kCTFontItalicTrait);
    if (!copied && (want & kCTFontBoldTrait))
      copied = CTFontCreateCopyWithSymbolicTraits(named, 0, nullptr, kCTFontBoldTrait,
                                                  kCTFontBoldTrait);
    if (copied) {
      CFRelease(named);
      styled = copied;
    }
  }
  if (italic && (CTFontGetSymbolicTraits(styled) & kCTFontItalicTrait) == 0) *synthetic = true;
  CTFontRef sized = FontAtEm(styled);
  CFRelease(styled);
  return sized;
}
bool Contains(const SelectedFont& font, uint32_t codepoint) {
  uint16_t glyph = 0;
  return font.face && SUCCEEDED(font.face->GetGlyphIndices(&codepoint, 1, &glyph)) && glyph;
}
CFStringRef CodepointString(uint32_t codepoint) {
  UniChar chars[2];
  CFIndex length = 1;
  if (codepoint >= 0x10000 && codepoint <= 0x10FFFF) {
    const uint32_t value = codepoint - 0x10000;
    chars[0] = UniChar(0xD800 + (value >> 10));
    chars[1] = UniChar(0xDC00 + (value & 0x3FF));
    length = 2;
  } else if (codepoint <= 0xFFFF) {
    chars[0] = UniChar(codepoint);
  } else {
    return nullptr;
  }
  return CFStringCreateWithCharacters(kCFAllocatorDefault, chars, length);
}
}

bool FontCollection::Initialize() { return true; }

bool FontCollection::Select(const FontInput& input, uint32_t codepoint, SelectedFont* out,
                            bool* missing) {
  if (missing) *missing = false;
  if (!out || codepoint > 0x10FFFF) return false;
  if (const auto generation = FontConfigGeneration(); generation != config_generation) {
    config_generation = generation;
    selected.clear();
    families.clear();
    fallbacks.clear();
  }
  const std::string family_key = input.family + "\n" + input.language;
  auto memo = families.find(family_key);
  if (memo == families.end()) {
    if (families.size() >= 256) families.clear();
    memo = families.emplace(family_key, FontFamilies(input)).first;
  }
  for (const auto& name : memo->second) {
    if (name.empty() || !FontFamilyVisible(name)) continue;
    bool cached = false;
    for (const auto& font : selected) {
      if (font.family != name || font.weight != input.weight || font.italic != input.italic)
        continue;
      cached = true;
      if (Contains(font, codepoint)) {
        *out = font;
        return true;
      }
      break;
    }
    if (cached) continue;
    bool synthetic = false;
    CTFontRef created = MakeFont(name, input.weight, input.italic, &synthetic);
    if (!created) {
      const auto& aliases = Aliases();
      const auto found = aliases.substitutes.find(LowerAscii(name));
      if (found != aliases.substitutes.end())
        created = MakeFont(found->second, input.weight, input.italic, &synthetic);
    }
    if (!created) continue;
    SelectedFont font;
    font.face.Attach(new IDWriteFontFace(created));
    font.family = name;
    font.weight = input.weight;
    font.italic = input.italic;
    font.syntheticItalic = synthetic;
    if (selected.size() < 32) selected.push_back(font);
    if (Contains(font, codepoint)) {
      *out = font;
      return true;
    }
  }
  const uint64_t fallback_key = (uint64_t(codepoint) << 16) |
                                (uint64_t(input.weight & 0xFFFF) << 1) | uint64_t(input.italic);
  if (const auto known = fallbacks.find(fallback_key); known != fallbacks.end()) {
    if (!known->second.face) {
      if (missing) *missing = true;
      return false;
    }
    *out = known->second;
    return true;
  }
  if (fallbacks.size() >= 4096) fallbacks.clear();
  CTFontRef base = CTFontCreateWithName(CFSTR(".AppleSystemUIFont"), 0, nullptr);
  CFStringRef sample = CodepointString(codepoint);
  CTFontRef cascade = base && sample ? CTFontCreateForString(base, sample, CFRangeMake(0, CFStringGetLength(sample)))
                                    : nullptr;
  if (sample) CFRelease(sample);
  if (base) CFRelease(base);
  CTFontRef sized = FontAtEm(cascade);
  if (cascade) CFRelease(cascade);
  SelectedFont font;
  if (sized && !LastResort(sized)) {
    font.face.Attach(new IDWriteFontFace(sized));
    font.weight = input.weight;
    font.italic = input.italic;
    font.syntheticItalic = input.italic;
    if (Contains(font, codepoint)) {
      fallbacks[fallback_key] = font;
      *out = font;
      return true;
    }
  } else if (sized) {
    CFRelease(sized);
  }
  fallbacks[fallback_key] = SelectedFont{};
  if (missing) *missing = true;
  return false;
}
}  // namespace ruyijs::web_geometry
#else
#include "web_geometry/font_backend.h"
#include "web_geometry/font_config.h"
#pragma comment(lib, "dwrite.lib")

namespace ruyijs::web_geometry {
bool FontCollection::Initialize() {
  return SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory),
      reinterpret_cast<IUnknown**>(factory.GetAddressOf()))) &&
      SUCCEEDED(factory->GetSystemFontCollection(&fonts));
}
namespace {
bool Failed(HRESULT result, bool* error) {
  if (SUCCEEDED(result)) return false;
  *error = true;
  return true;
}
std::wstring FamilyName(const ComPtr<IDWriteFontFamily>& family, bool* error) {
  ComPtr<IDWriteLocalizedStrings> names;
  UINT32 length = 0;
  if (Failed(family->GetFamilyNames(&names), error) || names->GetCount() == 0 ||
      Failed(names->GetStringLength(0, &length), error)) return {};
  std::wstring out(length + 1, L'\0');
  if (Failed(names->GetString(0, out.data(), length + 1), error)) return {};
  out.resize(length);
  return out;
}
bool Contains(const SelectedFont& font, uint32_t codepoint, bool* error) {
  UINT16 glyph = 0;
  return !Failed(font.face->GetGlyphIndices(&codepoint, 1, &glyph), error) && glyph;
}
}
bool FontCollection::Select(const FontInput& input, uint32_t codepoint, SelectedFont* out,
                             bool* missing) {
  if (missing) *missing = false;
  bool backendError = false;
  const auto failed = [&](HRESULT result) { return Failed(result, &backendError); };
  if (const auto generation = FontConfigGeneration(); generation != config_generation) {
    config_generation = generation;
    selected.clear(); families.clear(); fallbacks.clear();
  }
  const std::string familyKey = input.family + '\0' + input.language;
  auto memo = families.find(familyKey);
  if (memo == families.end()) {
    if (families.size() >= 256) families.clear();
    memo = families.emplace(familyKey, FontFamilies(input)).first;
  }
  // The family loop over one list (gfxFontGroup's font list or a
  // pref-lang fallback list): the first visible family whose face for this
  // weight/style has the character.
  bool aborted = false;
  bool resolved = false;  // some family of the list exists (gfxFontGroup::mFonts non-empty)
  const auto tryNames = [&](const std::vector<std::wstring>& names) -> bool {
  for (const auto& name : names) {
    if (!FontFamilyVisible(name)) continue;
    bool cached = false;
    for (const auto& font : selected) {
      if (font.family != name || font.weight != input.weight || font.italic != input.italic) continue;
      cached = true; resolved = true;
      if (Contains(font, codepoint, &backendError)) { *out = font; return true; }
      break;
    }
    if (cached) continue;
    UINT32 index = 0; BOOL exists = FALSE;
    std::wstring lookup = name;
    ComPtr<IDWriteFontFamily> family;
    ComPtr<IDWriteFont> match;
    SelectedFont font;
    if (failed(fonts->FindFamilyName(lookup.c_str(), &index, &exists)) || !exists) {
      std::wstring key = name;
      for (auto& ch : key) ch = wchar_t(towlower(ch));
      const auto& aliases = Aliases();
      if (const auto sub = aliases.substitutes.find(key); sub != aliases.substitutes.end()) {
        lookup = sub->second;
        if (failed(fonts->FindFamilyName(lookup.c_str(), &index, &exists)) || !exists) continue;
      } else if (const auto faces = aliases.faces.find(key); faces != aliases.faces.end()) {
        match = AliasFace(faces->second, input.weight, input.italic);
        if (!match) { backendError = true; continue; }
        if (failed(match->GetFontFamily(&family))) continue;
      } else continue;
    }
    if ((!family && failed(fonts->GetFontFamily(index, &family))) ||
        (!match && failed(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT(input.weight), DWRITE_FONT_STRETCH_NORMAL,
            input.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, &match))) ||
        failed(match->CreateFontFace(&font.face))) continue;
    resolved = true;
    font.family = name; font.weight = input.weight; font.italic = input.italic;
    font.syntheticItalic = input.italic && ((font.face->GetSimulations() & DWRITE_FONT_SIMULATIONS_OBLIQUE) ||
                                         match->GetStyle() == DWRITE_FONT_STYLE_NORMAL);
    // HarfBuzz consumes original tables; the donor applies synthetic ink skew itself.
    if (font.syntheticItalic) {
      if (FAILED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT(input.weight), DWRITE_FONT_STRETCH_NORMAL,
          DWRITE_FONT_STYLE_NORMAL, &match)) || FAILED(match->CreateFontFace(&font.face))) { aborted = true; return false; }
    }
    if (selected.size() < 32) selected.push_back(font);
    if (Contains(font, codepoint, &backendError)) { *out = font; return true; }
  }
    return false;
  };
  if (tryNames(memo->second)) return true;
  if (aborted) return false;
  // gfxFontGroup::FindFontForChar: with no family of the list installed, the
  // platform default font (gfxDWriteFontList::GetDefaultFontForPlatform: Arial)
  // comes before the pref-lang and system fallback.
  if (!resolved) {
    static const std::vector<std::wstring> kDefault = {L"Arial"};
    if (tryNames(kDefault)) return true;
    if (aborted) return false;
  }
  // gfxFontGroup::FindFontForChar: an emoji-presentation character tries the
  // emoji preference list, then every character tries the pref-lang lists
  // (WhichPrefFontSupportsChar) before the system fallback.
  if (codepoint >= 0x1F000 || (codepoint >= 0x2600 && codepoint <= 0x27BF)) {
    if (tryNames(EmojiFallbackFamilies())) return true;
    if (aborted) return false;
  }
  if (tryNames(PrefFallbackFamilies(codepoint, input))) return true;
  if (aborted) return false;
  // gfxPlatformFontList::SystemFindFontForChar: CommonFontFallback first.
  if (tryNames(CommonFallbackFamilies(codepoint))) return true;
  if (aborted) return false;
  // Platform fallback queries the live collection, with no case-specific font names.
  const uint64_t fallbackKey = (uint64_t(codepoint) << 12) | (uint64_t(input.weight & 0x7ff) << 1) | input.italic;
  if (const auto known = fallbacks.find(fallbackKey); known != fallbacks.end()) {
    if (!known->second.face) { if (missing) *missing = !backendError; return false; }
    *out = known->second;
    return true;
  }
  if (fallbacks.size() >= 4096) fallbacks.clear();
  const auto count = fonts->GetFontFamilyCount();
  for (UINT32 index = 0; index < count; ++index) {
    ComPtr<IDWriteFontFamily> family; ComPtr<IDWriteFont> match;
    if (failed(fonts->GetFontFamily(index, &family)) ||
        failed(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT(input.weight), DWRITE_FONT_STRETCH_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, &match))) continue;
    if (!FontFamilyVisible(FamilyName(family, &backendError))) continue;
    BOOL contains = FALSE;
    if (failed(match->HasCharacter(codepoint, &contains)) || !contains) continue;
    SelectedFont font;
    if (failed(match->CreateFontFace(&font.face))) continue;
    font.italic = input.italic; font.syntheticItalic = input.italic;
    fallbacks[fallbackKey] = font;
    *out = std::move(font); return true;
  }
  if (!backendError) fallbacks[fallbackKey] = SelectedFont{};
  if (missing) *missing = !backendError;
  return false;
}
}
#endif
