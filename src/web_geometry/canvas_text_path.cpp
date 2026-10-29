#ifndef _WIN32
// Glyph outlines from Core Text. Coordinates are converted to the y-down
// space the Windows DirectWrite sink already reports, then divided by the
// canvas scale. Synthetic italic uses the same 14 degree skew.
#include "web_geometry/font_backend.h"

#include <CoreText/CoreText.h>

#include <cmath>
#include <utility>
#include <vector>

namespace ruyijs::web_geometry {
namespace {
constexpr size_t kPointLimit = 2u * 1024u * 1024u;

struct ApplyState {
  CanvasTextPath* path;
  float origin_x, origin_y, inverse, skew;
  bool ok;
};

void ApplyPath(void* info, const CGPathElement* element) {
  auto* state = static_cast<ApplyState*>(info);
  if (!state->ok) return;
  const auto map = [&](CGPoint point) {
    const float y_down = -float(point.y);
    const float x = float(point.x) - state->skew * y_down;
    return std::pair<float, float>{state->origin_x + x * state->inverse,
                                   state->origin_y + y_down * state->inverse};
  };
  const auto room = [&](size_t pairs) {
    if (state->path->points.size() + pairs * 2 > kPointLimit) state->ok = false;
    return state->ok;
  };
  switch (element->type) {
    case kCGPathElementMoveToPoint:
      if (!room(1)) return;
      {
        const auto [x, y] = map(element->points[0]);
        state->path->Move(x, y);
      }
      break;
    case kCGPathElementAddLineToPoint:
      if (!room(1)) return;
      {
        const auto [x, y] = map(element->points[0]);
        state->path->Line(x, y);
      }
      break;
    case kCGPathElementAddQuadCurveToPoint:
      if (!room(2)) return;
      {
        const auto [x1, y1] = map(element->points[0]);
        const auto [x2, y2] = map(element->points[1]);
        state->path->Quad(x1, y1, x2, y2);
      }
      break;
    case kCGPathElementAddCurveToPoint:
      if (!room(3)) return;
      {
        const auto [x1, y1] = map(element->points[0]);
        const auto [x2, y2] = map(element->points[1]);
        const auto [x3, y3] = map(element->points[2]);
        state->path->Cubic(x1, y1, x2, y2, x3, y3);
      }
      break;
    case kCGPathElementCloseSubpath:
      state->path->Close();
      break;
  }
}

uint32_t Codepoint(const std::u16string& text, size_t* at) {
  uint32_t code = text[(*at)++];
  if (code >= 0xd800 && code <= 0xdbff && *at < text.size() && text[*at] >= 0xdc00 &&
      text[*at] <= 0xdfff)
    code = 0x10000 + ((code - 0xd800) << 10) + text[(*at)++] - 0xdc00;
  return code;
}
bool Continuation(uint32_t code) {
  return code == 0x200d || (code >= 0xfe00 && code <= 0xfe0f) || (code >= 0xe0100 && code <= 0xe01ef) ||
         hb_unicode_combining_class(hb_unicode_funcs_get_default(), code) != 0;
}
}  // namespace

bool SystemFonts::BuildCanvasTextPath(const std::u16string& text, const FontInput& input,
                                      CanvasTextPath* result) {
  if (!result) return false;
  *result = {};
  if (text.empty()) return true;
  if (text.size() > 65536 || !std::isfinite(input.scale) || input.scale <= 0 || !std::isfinite(input.size) ||
      input.size <= 0 || !std::isfinite(input.x) || !std::isfinite(input.y))
    return false;
  if (!collection_) {
    auto collection = std::make_unique<FontCollection>();
    if (!collection->Initialize()) return false;
    collection_ = std::move(collection);
  }
  const float emsize = float(std::round(input.size * input.scale * 60.0) / 60.0);
  if (!std::isfinite(emsize) || emsize <= 0 || emsize > 16384) return false;
  const float skew = std::tan(14.0f * 3.14159265358979323846f / 180.0f);
  int pen = 0;
  for (size_t start = 0; start < text.size();) {
    size_t end = start;
    SelectedFont font;
    const uint32_t first = Codepoint(text, &end);
    if (!collection_->Select(input, first, &font)) return false;
    while (end < text.size()) {
      size_t next = end;
      const uint32_t code = Codepoint(text, &next);
      if (!Continuation(code)) {
        SelectedFont candidate;
        if (!collection_->Select(input, code, &candidate)) return false;
        if (font.face.Get() != candidate.face.Get() || font.syntheticItalic != candidate.syntheticItalic) break;
      }
      end = next;
    }
    FontInput local = input;
    local.x = local.y = 0;
    FontRun run;
    if (!ShapeFont(font, local, text.substr(start, end - start), &run)) return false;
    if (!run.glyphs.empty()) {
      CTFontRef base = static_cast<CTFontRef>(font.face->CoreFont());
      CTFontRef sized = base ? CTFontCreateCopyWithAttributes(base, emsize, nullptr, nullptr) : nullptr;
      if (!sized) return false;
      const float origin_x = float(input.x + double(pen) / 60.0 / input.scale);
      float glyph_pen = 0;
      for (size_t i = 0; i < run.glyphs.size(); ++i) {
        const float dx = i < run.glyph_offsets_x.size() ? run.glyph_offsets_x[i] : 0;
        const float dy = i < run.glyph_offsets_y.size() ? run.glyph_offsets_y[i] : 0;
        CGAffineTransform shift = CGAffineTransformMakeTranslation(glyph_pen + dx, dy);
        CGPathRef outline = CTFontCreatePathForGlyph(sized, CGGlyph(run.glyphs[i]), &shift);
        if (outline) {
          ApplyState state{result, origin_x, float(input.y), 1.0f / float(input.scale),
                           font.syntheticItalic ? skew : 0.0f, true};
          CGPathApply(outline, &state, ApplyPath);
          CFRelease(outline);
          if (!state.ok) {
            CFRelease(sized);
            return false;
          }
        }
        if (i < run.glyph_advances.size()) glyph_pen += run.glyph_advances[i];
      }
      CFRelease(sized);
    }
    pen += run.advance;
    start = end;
  }
  return true;
}
}  // namespace ruyijs::web_geometry
#else
#include "web_geometry/font_backend.h"

#include <d2d1.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace ruyijs::web_geometry {
namespace {
constexpr size_t kPointLimit = 2u * 1024u * 1024u;

class OutlineSink final : public ID2D1SimplifiedGeometrySink {
 public:
  OutlineSink(CanvasTextPath* path, float x, float y, float scale, bool italic)
      : path_(path), x_(x), y_(y), inverse_(1.0f / scale), italic_(italic) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (iid == __uuidof(IUnknown) || iid == __uuidof(ID2D1SimplifiedGeometrySink)) {
      *out = static_cast<ID2D1SimplifiedGeometrySink*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG refs = --refs_;
    if (!refs) delete this;
    return refs;
  }
  void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE) override {}
  void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) override {}
  void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F point, D2D1_FIGURE_BEGIN) override {
    if (!Valid() || path_->points.size() >= kPointLimit) {
      valid_ = false;
      return;
    }
    const auto mapped = Map(point);
    path_->Move(mapped.x, mapped.y);
  }
  void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F* points, UINT32 count) override {
    if (!Valid() || !points || count > kPointLimit - path_->points.size()) {
      valid_ = false;
      return;
    }
    for (UINT32 i = 0; i < count; ++i) {
      const auto mapped = Map(points[i]);
      path_->Line(mapped.x, mapped.y);
    }
  }
  void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT* beziers, UINT32 count) override {
    if (!Valid() || !beziers || count > (kPointLimit - path_->points.size()) / 3) {
      valid_ = false;
      return;
    }
    for (UINT32 i = 0; i < count; ++i) {
      const auto& b = beziers[i];
      const auto p1 = Map(b.point1), p2 = Map(b.point2), p3 = Map(b.point3);
      path_->Cubic(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y);
    }
  }
  void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END end) override {
    if (Valid() && end == D2D1_FIGURE_END_CLOSED) path_->Close();
  }
  HRESULT STDMETHODCALLTYPE Close() override { return valid_ ? S_OK : E_OUTOFMEMORY; }
  bool Valid() const { return valid_ && path_ && path_->points.size() <= kPointLimit; }

 private:
  CanvasTextPath* path_;
  float x_, y_, inverse_;
  bool italic_, valid_ = true;
  ULONG refs_ = 1;
  D2D1_POINT_2F Map(D2D1_POINT_2F point) const {
    const float skew = italic_ ? std::tan(14.0f * 3.14159265358979323846f / 180.0f) : 0.0f;
    const float x = point.x - skew * point.y;
    return {x_ + x * inverse_, y_ + point.y * inverse_};
  }
};

uint32_t Codepoint(const std::u16string& text, size_t* at) {
  uint32_t code = text[(*at)++];
  if (code >= 0xd800 && code <= 0xdbff && *at < text.size() &&
      text[*at] >= 0xdc00 && text[*at] <= 0xdfff)
    code = 0x10000 + ((code - 0xd800) << 10) + text[(*at)++] - 0xdc00;
  return code;
}
bool Continuation(uint32_t code) {
  return code == 0x200d || (code >= 0xfe00 && code <= 0xfe0f) ||
      (code >= 0xe0100 && code <= 0xe01ef) ||
      hb_unicode_combining_class(hb_unicode_funcs_get_default(), code) != 0;
}
}  // namespace

bool SystemFonts::BuildCanvasTextPath(const std::u16string& text, const FontInput& input,
                                      CanvasTextPath* result) {
  if (!result) return false;
  *result = {};
  if (text.empty()) return true;
  if (text.size() > 65536 || !std::isfinite(input.scale) || input.scale <= 0 ||
      !std::isfinite(input.size) || input.size <= 0 || !std::isfinite(input.x) ||
      !std::isfinite(input.y)) return false;
  if (!collection_) {
    auto collection = std::make_unique<FontCollection>();
    if (!collection->Initialize()) return false;
    collection_ = std::move(collection);
  }
  const float emsize = float(std::round(input.size * input.scale * 60.0) / 60.0);
  if (!std::isfinite(emsize) || emsize <= 0 || emsize > 16384) return false;
  int pen = 0;
  for (size_t start = 0; start < text.size();) {
    size_t end = start;
    SelectedFont font;
    const uint32_t first = Codepoint(text, &end);
    if (!collection_->Select(input, first, &font)) return false;
    while (end < text.size()) {
      size_t next = end;
      const uint32_t code = Codepoint(text, &next);
      if (!Continuation(code)) {
        SelectedFont candidate;
        if (!collection_->Select(input, code, &candidate)) return false;
        if (font.face.Get() != candidate.face.Get() ||
            font.syntheticItalic != candidate.syntheticItalic)
          break;
      }
      end = next;
    }
    FontInput local = input;
    local.x = local.y = 0;
    FontRun run;
    if (!ShapeFont(font, local, text.substr(start, end - start), &run)) return false;
    if (!run.glyphs.empty()) {
      std::vector<DWRITE_GLYPH_OFFSET> offsets(run.glyphs.size());
      for (size_t i = 0; i < offsets.size(); ++i) {
        offsets[i].advanceOffset = run.glyph_offsets_x[i];
        offsets[i].ascenderOffset = run.glyph_offsets_y[i];
      }
      const float x = float(input.x + double(pen) / 60.0 / input.scale);
      auto* sink = new OutlineSink(result, x, float(input.y), float(input.scale),
                                   font.syntheticItalic);
      const HRESULT hr = font.face->GetGlyphRunOutline(
          emsize, run.glyphs.data(), run.glyph_advances.data(), offsets.data(),
          UINT32(run.glyphs.size()), FALSE, FALSE, sink);
      const bool valid = SUCCEEDED(hr) && sink->Valid();
      sink->Release();
      if (!valid) return false;
    }
    pen += run.advance;
    start = end;
  }
  return true;
}
}  // namespace ruyijs::web_geometry

#endif
