// roundRect(): RoundRectImpl's argument steps; the path itself is built by the
// Skia bridge from the recorded op 11 (AppendRoundedRectToPath, four Béziers).
#include "web_canvas/internal.h"
#include <algorithm>
#include <vector>
#include "js/ErrorReport.h"
#include "js/ForOfIterator.h"
#include "js/Symbol.h"
#include <cmath>

namespace ruyijs::web_canvas {
namespace {
// One member of the radii union: an unrestricted double, or a DOMPointInit
// whose members WebIDL converts in w, x, y, z order.
struct Radius { double x = 0, y = 0; bool point = false; };
bool ReadRadius(JSContext* cx, JS::HandleValue value, Radius* out) {
  if (value.isObject() || value.isNullOrUndefined()) {
    out->point = true;
    if (value.isNullOrUndefined()) return true;
    JS::RootedObject object(cx, &value.toObject());
    const char* names[] = {"w", "x", "y", "z"};
    for (const char* name : names) {
      JS::RootedValue member(cx);
      double number = 0;
      if (!JS_GetProperty(cx, object, name, &member)) return false;
      if (!member.isUndefined() && !JS::ToNumber(cx, member, &number)) return false;
      if (name[0] == 'x' && !member.isUndefined()) out->x = number;
      if (name[0] == 'y' && !member.isUndefined()) out->y = number;
    }
    return true;
  }
  if (!JS::ToNumber(cx, value, &out->x)) return false;
  out->y = out->x;
  return true;
}
// The union conversion the binding does before the method runs.
bool ConvertRadii(JSContext* cx, JS::HandleValue value, std::vector<Radius>* values,
                  bool* sequence) {
  *sequence = false;
  if (value.isObject()) {
    JS::RootedObject object(cx, &value.toObject());
    JS::RootedId key(cx, JS::PropertyKey::Symbol(JS::GetWellKnownSymbol(cx, JS::SymbolCode::iterator)));
    JS::RootedValue iter(cx);
    if (!JS_GetPropertyById(cx, object, key, &iter)) return false;
    *sequence = !iter.isNullOrUndefined();
  }
  if (!*sequence) {
    Radius radius;
    if (!ReadRadius(cx, value, &radius)) return false;
    values->push_back(radius);
    return true;
  }
  JS::ForOfIterator iter(cx);
  if (!iter.init(value)) return false;
  for (;;) {
    bool done;
    JS::RootedValue item(cx);
    if (!iter.next(&item, &done)) return false;
    if (done) break;
    if (values->size() >= 1u << 16) return WebKit::FailKind(cx, "roundRect radii limit", JSEXN_RANGEERR);
    Radius radius;
    if (!ReadRadius(cx, item, &radius)) return false;
    values->push_back(radius);
  }
  return true;
}
}  // namespace

bool RoundRect(JSContext* cx, unsigned argc, JS::Value* vp) {
  auto args = JS::CallArgsFromVp(argc, vp);
  const bool path = detail::ContextSlot(args) == 100;
  JS::RootedObject self(cx);
  if (path ? !WebKit::Check(cx, args.thisv(), &kPath2DClass, &self, "Illegal invocation")
           : !detail::ContextReceiver(cx, args, &self)) return false;
  if (argc < 4) return WebKit::Fail(cx, "roundRect requires four coordinates");
  double rect[4];
  for (int i = 0; i < 4; ++i) if (!JS::ToNumber(cx, args[i], &rect[i])) return false;
  std::vector<Radius> values;
  bool sequence = false;
  JS::RootedValue radii(cx, argc > 4 ? args[4] : JS::NumberValue(0));
  if (!ConvertRadii(cx, radii, &values, &sequence)) return false;
  args.rval().setUndefined();
  // Step 1: a non-finite rectangle draws nothing.
  for (double n : rect) if (!std::isfinite(n)) return true;
  // Step 3: one to four radii.
  if (sequence && (values.empty() || values.size() > 4))
    return WebKit::FailKind(cx, "Can have between 1 and 4 radii", JSEXN_RANGEERR);
  // Step 5: in order, a non-finite radius returns and a negative one throws.
  for (const Radius& radius : values) {
    if (!std::isfinite(radius.x) || !std::isfinite(radius.y)) return true;
    if (radius.x < 0 || radius.y < 0)
      return WebKit::FailKind(cx, "Radius can not be negative", JSEXN_RANGEERR);
  }
  // Steps 7-10: upperLeft, upperRight, lowerRight, lowerLeft.
  const size_t n = values.size();
  const Radius ul = values[0];
  const Radius ur = n > 1 ? values[1] : values[0];
  const Radius lr = n > 2 ? values[2] : values[0];
  const Radius ll = n > 3 ? values[3] : n > 1 ? values[1] : values[0];
  const double command[13] = {11.0, rect[0], rect[1], rect[2], rect[3],
                              ul.x, ul.y, ur.x, ur.y, lr.x, lr.y, ll.x, ll.y};
  return AppendCanvasPath(cx, self, command, 13);
}
}  // namespace ruyijs::web_canvas
