#pragma once
// gfxScriptItemizer's runs reduced to what glyph runs depend on: whether each
// UTF-16 unit belongs to a CJK script run. gfxTextRun::AddGlyphRun never merges
// a CJK script run's glyphs into a non-CJK one (GlyphRun::Matches compares
// mIsCJK), so canvas measurement and drawing break their runs there too.
// Common and inherited characters join the run they are in; leading ones join
// the first real script that follows.
#include <string>
#include <vector>

#include "hb.h"

namespace ruyijs::web_geometry {
inline bool IsCJKScript(hb_script_t script) {
  return script == HB_SCRIPT_BOPOMOFO || script == HB_SCRIPT_HAN || script == HB_SCRIPT_HANGUL ||
         script == HB_SCRIPT_HIRAGANA || script == HB_SCRIPT_KATAKANA;
}

inline std::vector<bool> CJKRunUnits(const std::u16string& text) {
  std::vector<bool> out(text.size(), false);
  hb_unicode_funcs_t* funcs = hb_unicode_funcs_get_default();
  hb_script_t current = HB_SCRIPT_COMMON;
  size_t runStart = 0;
  auto flush = [&](size_t end) {
    const bool cjk = IsCJKScript(current);
    for (size_t k = runStart; k < end; ++k) out[k] = cjk;
  };
  for (size_t i = 0; i < text.size();) {
    uint32_t cp = text[i];
    size_t width = 1;
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 &&
        text[i + 1] <= 0xDFFF) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (text[i + 1] - 0xDC00);
      width = 2;
    }
    const hb_script_t script = hb_unicode_script(funcs, cp);
    const bool common = script == HB_SCRIPT_COMMON || script == HB_SCRIPT_INHERITED;
    if (!common) {
      if (current == HB_SCRIPT_COMMON) {
        current = script;
      } else if (script != current) {
        flush(i);
        runStart = i;
        current = script;
      }
    }
    i += width;
  }
  flush(text.size());
  return out;
}
}  // namespace ruyijs::web_geometry
