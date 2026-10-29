#ifndef _WIN32
// CPAL palette 0, read through the Core Text table copy already used by the
// shaper. Record order is the OpenType BGRA colour Firefox's Windows path
// stores as ARGB.
#include "web_geometry/font_backend.h"

#include <vector>

namespace ruyijs::web_geometry {
namespace {
constexpr uint32_t OtTag(char a, char b, char c, char d) {
  return (uint32_t(uint8_t(d)) << 24) | (uint32_t(uint8_t(c)) << 16) | (uint32_t(uint8_t(b)) << 8) |
         uint32_t(uint8_t(a));
}
uint16_t U16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
uint32_t U32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
}  // namespace

bool GlyphPalette(IDWriteFontFace* face, std::vector<uint32_t>* colors) {
  if (!face || !colors) return false;
  const void* data = nullptr;
  UINT32 size = 0;
  void* context = nullptr;
  BOOL exists = FALSE;
  if (FAILED(face->TryGetFontTable(OtTag('C', 'P', 'A', 'L'), &data, &size, &context, &exists)) || !exists)
    return false;
  const auto* cpal = static_cast<const uint8_t*>(data);
  bool ok = false;
  if (size >= 12) {
    const uint16_t entries = U16(cpal + 2), palettes = U16(cpal + 4);
    const uint32_t records = U32(cpal + 8);
    const uint64_t index_at = 12;
    if (palettes && entries && index_at + uint64_t(palettes) * 2u <= size) {
      const uint32_t first = U16(cpal + index_at);
      if (uint64_t(first) + entries <= 0xffffffffu && uint64_t(records) + (uint64_t(first) + entries) * 4u <= size) {
        colors->resize(entries);
        const uint8_t* record = cpal + records + first * 4u;
        for (uint16_t i = 0; i < entries; ++i) {
          const uint8_t* bgra = record + i * 4u;
          (*colors)[i] = (uint32_t(bgra[3]) << 24) | (uint32_t(bgra[2]) << 16) | (uint32_t(bgra[1]) << 8) |
                         uint32_t(bgra[0]);
        }
        ok = true;
      }
    }
  }
  face->ReleaseFontTable(context);
  return ok;
}
}  // namespace ruyijs::web_geometry
#else
// The palette a colour font's COLR layers name: palette 0 of the CPAL table, the
// one Skia's DWriteFontTypeface keeps as its target palette.
#include <dwrite_2.h>

#include <cmath>
#include <vector>

#include "web_geometry/font_backend.h"

namespace ruyijs::web_geometry {
// The palette Skia reads once per typeface: palette 0 of the CPAL table.
bool GlyphPalette(IDWriteFontFace* face, std::vector<uint32_t>* colors) {
  ComPtr<IDWriteFontFace2> face2;
  face->QueryInterface(__uuidof(IDWriteFontFace2), reinterpret_cast<void**>(face2.GetAddressOf()));
  if (!face2) return false;
  const UINT32 count = face2->GetPaletteEntryCount();
  if (!count) return false;
  std::vector<DWRITE_COLOR_F> entries(count);
  if (FAILED(face2->GetPaletteEntries(0, 0, count, entries.data()))) return false;
  colors->resize(count);
  for (UINT32 i = 0; i < count; ++i) {
    const auto& c = entries[i];
    const auto byte = [](float v) {
      return uint8_t(v <= 0 ? 0 : v >= 1 ? 255 : int(std::floor(v * 255.0f + 0.5f)));
    };
    (*colors)[i] = (uint32_t(byte(c.a)) << 24) | (uint32_t(byte(c.r)) << 16) |
                   (uint32_t(byte(c.g)) << 8) | uint32_t(byte(c.b));
  }
  return true;
}
}  // namespace ruyijs::web_geometry

#endif
