// Canvas drawing resources: state lives only in these realm-owned instances.
#include "web_canvas/internal.h"
#include <cstring>
#include "js/Wrapper.h"
#include "web_canvas/raster_color.h"
#include "js/Realm.h"

namespace ruyijs::web_canvas {
// Slot 0: the raw stops as offset, r, g, b, a (straight bytes) quintuples.
// Slot 1: the geometry, kind (0 linear, 1 radial, 2 conic) then its numbers.
const JSClass kGradientClass = {"CanvasGradient", JSCLASS_HAS_RESERVED_SLOTS(2)};
// Slots: 0 the source, 1 the float setTransform() matrix, 2 the pixel
// snapshot taken at createPattern (premultiplied RGBA), 3/4 its size, 5 flags:
// bit 0 opaque surface, bits 1-2 CanvasPattern::RepeatMode.
const JSClass kPatternClass = {"CanvasPattern", JSCLASS_HAS_RESERVED_SLOTS(6)};
namespace {
bool ColorStop(JSContext* cx, unsigned argc, JS::Value* vp) {
  auto args = JS::CallArgsFromVp(argc, vp);
  State& s = *detail::Self(args);
  JS::RootedObject self(cx);
  if (!WebKit::CheckBrand(cx, args, &kGradientClass, &self, "CanvasGradient")) return false;
  if (argc < 2) return WebKit::Fail(cx, "addColorStop requires offset and color");
  // WebIDL `float offset, UTF8String color`, then CanvasGradient::AddColorStop.
  double number;
  if (!JS::ToNumber(cx, args[0], &number)) return false;
  const float offset = float(number);
  if (!std::isfinite(offset)) return WebKit::Fail(cx, "CanvasGradient.addColorStop: Argument 1 is not a finite floating-point value.");
  JS::RootedString color(cx, JS::ToString(cx, args[1]));
  if (!color) return false;
  if (offset < 0.0 || offset > 1.0)
    return ThrowDOMException(s, "IndexSizeError", "Offset out of 0-1.0 range");
  std::string ascii;
  uint8_t rgba[4];
  if (!detail::StringToAscii(cx, color, &ascii)) return false;
  if (ascii.empty() || !raster::ParseCSSColor(ascii, rgba))
    return ThrowDOMException(s, "SyntaxError", "Invalid color");
  JS::RootedObject stops(cx, &JS::GetReservedSlot(self, 0).toObject());
  uint32_t n = 0;
  if (!JS::GetArrayLength(cx, stops, &n)) return false;
  args.rval().setUndefined();
  if (n >= 8192 * 5) return true;
  const double values[5] = {offset, double(rgba[0]), double(rgba[1]), double(rgba[2]), double(rgba[3])};
  for (uint32_t i = 0; i < 5; ++i) {
    JS::RootedValue value(cx, JS::NumberValue(values[i]));
    if (!JS_SetElement(cx, stops, n + i, value)) return false;
  }
  return true;
}
bool PatternTransform(JSContext* cx, unsigned argc, JS::Value* vp) {
  auto args = JS::CallArgsFromVp(argc, vp);
  JS::RootedObject self(cx);
  if (!WebKit::CheckBrand(cx, args, &kPatternClass, &self, "CanvasPattern")) return false;
  JS::RootedObject matrix(cx, JS::NewArrayObject(cx, 6));
  if (!matrix) return false;
  const char* names[] = {"a", "b", "c", "d", "e", "f"};
  for (int i = 0; i < 6; ++i) {
    JS::RootedValue v(cx);
    if (args.get(0).isObject()) {
      JS::RootedObject init(cx, &args[0].toObject());
      if (!JS_GetProperty(cx, init, names[i], &v)) return false;
    }
    double number = i == 0 || i == 3 ? 1 : 0;
    if (!v.isUndefined() && !JS::ToNumber(cx, v, &number)) return false;
    if (!std::isfinite(number)) { args.rval().setUndefined(); return true; }
    v.setNumber(double(float(number)));  // CanvasPattern::mTransform is a float Matrix
    if (!JS_SetElement(cx, matrix, i, v)) return false;
  }
  JS::SetReservedSlot(self, 1, JS::ObjectValue(*matrix));
  args.rval().setUndefined();
  return true;
}
}
bool CreateCanvasResource(JSContext* cx, unsigned argc, JS::Value* vp) {
  auto args = JS::CallArgsFromVp(argc, vp);
  State& s = *detail::Self(args);
  int op = detail::ContextSlot(args);
  JS::RootedObject self(cx);
  if (!detail::ContextReceiver(cx, args, &self)) return false;
  if (op == 3) {
    if (argc < 2 || !args[0].isObject()) return WebKit::Fail(cx, "createPattern requires an image");
    JS::RootedString repeat(cx, JS::ToString(cx, args[1]));
    if (!repeat) return false;
    bool valid = args[1].isNull();
    for (const char* key : {"", "repeat", "repeat-x", "repeat-y", "no-repeat"}) {
      bool match = false;
      if (!detail::StringEqualsAscii(cx, repeat, key, &match)) return false;
      valid |= match;
    }
    if (!valid) return ThrowDOMException(s, "SyntaxError", "Invalid repetition");
    // CreatePattern: "" / "repeat" REPEAT, then REPEATX, REPEATY, NOREPEAT
    // (null stringifies to "null", which is not a keyword above).
    int mode = 0;
    for (int index = 1; index <= 3; ++index) {
      const char* keys[] = {"", "repeat-x", "repeat-y", "no-repeat"};
      bool match = false;
      if (!detail::StringEqualsAscii(cx, repeat, keys[index], &match)) return false;
      if (match) mode = index;
    }
    // A canvas with a zero dimension throws; otherwise the source surface is
    // snapshotted now, so later drawing into the source does not show.
    JS::RootedObject source(cx, js::CheckedUnwrapStatic(&args[0].toObject()));
    if (source) {
      if (CanvasBinding* binding = s.Find(source)) {
        if (binding->width == 0)
          return ThrowDOMException(s, "InvalidStateError", "Passed-in canvas has width 0");
        if (binding->height == 0)
          return ThrowDOMException(s, "InvalidStateError", "Passed-in canvas has height 0");
      }
    }
    detail::SourcePixels pixels;
    if (!detail::ReadImagePixels(s, args[0], &pixels)) {
      args.rval().setNull();
      return true;
    }
    JS::RootedObject object(cx, s.kit.New(&kPatternClass, s.pattern.prototype));
    JS::RootedObject snapshot(cx, JS_NewUint8Array(cx, pixels.bytes.size()));
    if (!object || !snapshot) return false;
    if (!pixels.bytes.empty()) {
      JS::AutoCheckCannotGC nogc;
      std::memcpy(detail::ViewBytes(snapshot), pixels.bytes.data(), pixels.bytes.size());
    }
    JS::SetReservedSlot(object, 0, args[0]);
    JS::SetReservedSlot(object, 1, JS::UndefinedValue());
    JS::SetReservedSlot(object, 2, JS::ObjectValue(*snapshot));
    JS::SetReservedSlot(object, 3, JS::NumberValue(pixels.width));
    JS::SetReservedSlot(object, 4, JS::NumberValue(pixels.height));
    JS::SetReservedSlot(object, 5, JS::Int32Value((pixels.opaque ? 1 : 0) | (mode << 1)));
    args.rval().setObject(*object);
    return true;
  }
  unsigned count = op == 0 ? 4 : op == 1 ? 6 : 3;
  if (argc < count) return WebKit::Fail(cx, "Missing gradient arguments");
  double values[6]; bool usable;
  if (!detail::ReadNumbers(cx, args, 0, count, values, &usable)) return false;
  if (!usable) return WebKit::Fail(cx, "Gradient arguments must be finite");
  if (op == 1 && (values[2] < 0 || values[5] < 0))
    return ThrowDOMException(s, "IndexSizeError", "Negative radius");
  JS::RootedObject object(cx, s.kit.New(&kGradientClass, s.gradient.prototype));
  JS::RootedObject stops(cx, JS::NewArrayObject(cx, 0));
  JS::RootedObject geometry(cx, JS::NewArrayObject(cx, 0));
  if (!object || !stops || !geometry) return false;
  JS::RootedValue value(cx, JS::Int32Value(op));
  if (!JS_SetElement(cx, geometry, 0, value)) return false;
  // CreateConicGradient stores aAngle + pi / 2 (start angle from the y-axis).
  if (op == 2) values[0] += 3.14159265358979323846 / 2.0;
  for (unsigned i = 0; i < count; ++i) {
    value.setNumber(values[i]);
    if (!JS_SetElement(cx, geometry, i + 1, value)) return false;
  }
  JS::SetReservedSlot(object, 0, JS::ObjectValue(*stops));
  JS::SetReservedSlot(object, 1, JS::ObjectValue(*geometry));
  args.rval().setObject(*object);
  return true;
}
bool InstallCanvasResources(State& s) {
  JS::RootedObject parent(s.cx, JS::GetRealmObjectPrototype(s.cx));
  return s.kit.Define("CanvasGradient", &kGradientClass, parent, WebKit::Reject, &s.gradient) &&
    s.kit.Method(s.gradient, "CanvasGradient", "addColorStop", ColorStop, 2, 0) &&
    s.kit.Define("CanvasPattern", &kPatternClass, parent, WebKit::Reject, &s.pattern) &&
    s.kit.Method(s.pattern, "CanvasPattern", "setTransform", PatternTransform, 0, 0);
}
}
