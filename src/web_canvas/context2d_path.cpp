// The 2D path members and clearRect. The path members record the geometry the
// script builds into a command list kept on the context, which the Skia bridge
// replays; clearRect clears through the same bridge. Rasterizing, text,
// gradient and text resources have their own native member files (D35).
#include "web_canvas/internal.h"

#include <cmath>
#include <cstring>

namespace ruyijs::web_canvas {
namespace {
enum PathKind : uint8_t {
  kBegin, kClose, kMove, kLine, kRect, kArc, kEllipse, kQuadratic, kBezier, kClear,
};

struct MethodSpec {
  const char* name;
  PathKind kind;
  unsigned length;
};

const MethodSpec kMethods[] = {
    {"beginPath", kBegin, 0},         {"closePath", kClose, 0},
    {"moveTo", kMove, 2},             {"lineTo", kLine, 2},
    {"arc", kArc, 5},                 {"ellipse", kEllipse, 7},
    {"rect", kRect, 4},               {"quadraticCurveTo", kQuadratic, 4},
    {"bezierCurveTo", kBezier, 6},    {"clearRect", kClear, 4},
};

// Every command is appended as [opcode, ...numbers]. The record is bounded, so
// a script cannot grow it without limit.
constexpr double kOpMove = 0.0, kOpLine = 1.0, kOpClose = 2.0, kOpRect = 3.0,
                 kOpArc = 4.0, kOpEllipse = 5.0, kOpQuadratic = 6.0, kOpBezier = 7.0;
constexpr uint32_t kPathLimit = 8192;

JSObject* PathOf(JSContext* cx, JS::HandleObject context) {
  JS::Value slot = JS::GetReservedSlot(context, kContextPathSlot);
  return slot.isObject() ? &slot.toObject() : nullptr;
}

}  // namespace
bool AppendCanvasPath(JSContext* cx, JS::HandleObject context, const double* values, uint32_t count) {
  JS::RootedObject path(cx, PathOf(cx, context));
  if (!path) return false;
  uint32_t length = 0;
  if (!JS::GetArrayLength(cx, path, &length)) return false;
  if (length + count + 7 > kPathLimit) return true;
  double matrix[6] = {1, 0, 0, 1, 0, 0};
  if (JS::GetClass(context) != &kPath2DClass && !detail::GetMatrix(cx, context, matrix)) return false;
  JS::RootedValue opcode(cx, JS::NumberValue(8));
  if (!JS_SetElement(cx, path, length++, opcode)) return false;
  for (double component : matrix) {
    JS::RootedValue value(cx, JS::NumberValue(component));
    if (!JS_SetElement(cx, path, length++, value)) return false;
  }
  for (uint32_t index = 0; index < count; ++index) {
    JS::RootedValue value(cx, JS::NumberValue(values[index]));
    if (!JS_SetElement(cx, path, length + index, value)) return false;
  }
  return true;
}
namespace {

}  // namespace

bool Context2DPathMethod(JSContext* cx, unsigned argc, JS::Value* vp) {
  auto args = JS::CallArgsFromVp(argc, vp);
  const int32_t index = detail::ContextSlot(args);
  JS::RootedObject self(cx);
  if (index >= 100) {
    if (!WebKit::CheckBrand(cx, args, &kPath2DClass, &self, "Path2D")) return false;
  } else if (!detail::ContextReceiver(cx, args, &self)) return false;
  const MethodSpec& spec = kMethods[index >= 100 ? index - 100 : index];
  State& state = *detail::Self(args);
  args.rval().setUndefined();
  if (spec.kind == kBegin) {
    JS::RootedObject path(cx, PathOf(cx, self));
    return path && JS::SetArrayLength(cx, path, 0);
  }
  if (spec.kind == kClose) {
    const double command[1] = {kOpClose};
    return AppendCanvasPath(cx, self, command, 1);
  }
  if (spec.kind == kClear) {
    double rect[4];
    bool usable = false;
    if (!detail::ReadNumbers(cx, args, 0, 4, rect, &usable)) return false;
    return !usable || ClearRecordedRect(state, self, rect);
  }
  if (spec.kind == kMove || spec.kind == kLine) {
    double point[2];
    bool usable = false;
    if (!detail::ReadNumbers(cx, args, 0, 2, point, &usable)) return false;
    if (!usable) return true;
    const double command[3] = {spec.kind == kMove ? kOpMove : kOpLine, point[0], point[1]};
    return AppendCanvasPath(cx, self, command, 3);
  }
  if (spec.kind == kRect) {
    double rect[4];
    bool usable = false;
    if (!detail::ReadNumbers(cx, args, 0, 4, rect, &usable)) return false;
    if (!usable) return true;
    const double command[5] = {kOpRect, rect[0], rect[1], rect[2], rect[3]};
    return AppendCanvasPath(cx, self, command, 5);
  }
  if (spec.kind == kArc) {
    double arc[5];
    bool usable = false;
    if (!detail::ReadNumbers(cx, args, 0, 5, arc, &usable)) return false;
    if (!usable) return true;
    if (arc[2] < 0.0)
      return ThrowDOMException(state, "IndexSizeError", "The radius provided is negative.");
    const double command[7] = {kOpArc, arc[0], arc[1], arc[2], arc[3], arc[4],
                               JS::ToBoolean(args.get(5)) ? 1.0 : 0.0};
    return AppendCanvasPath(cx, self, command, 7);
  }
  if (spec.kind == kEllipse) {
    double ellipse[7];
    bool usable = false;
    if (!detail::ReadNumbers(cx, args, 0, 7, ellipse, &usable)) return false;
    if (!usable) return true;
    if (ellipse[2] < 0.0 || ellipse[3] < 0.0)
      return ThrowDOMException(state, "IndexSizeError", "The radius provided is negative.");
    const double command[9] = {kOpEllipse, ellipse[0], ellipse[1], ellipse[2], ellipse[3],
                               ellipse[4], ellipse[5], ellipse[6],
                               JS::ToBoolean(args.get(7)) ? 1.0 : 0.0};
    return AppendCanvasPath(cx, self, command, 9);
  }
  if (spec.kind == kQuadratic) {
    double curve[4];
    bool usable = false;
    if (!detail::ReadNumbers(cx, args, 0, 4, curve, &usable)) return false;
    if (!usable) return true;
    const double command[5] = {kOpQuadratic, curve[0], curve[1], curve[2], curve[3]};
    return AppendCanvasPath(cx, self, command, 5);
  }
  double curve[6];
  bool usable = false;
  if (!detail::ReadNumbers(cx, args, 0, 6, curve, &usable)) return false;
    if (!usable) return true;
  const double command[7] = {kOpBezier, curve[0], curve[1], curve[2],
                             curve[3], curve[4], curve[5]};
  return AppendCanvasPath(cx, self, command, 7);
}

bool InstallContext2DPath(State& state, WebFace& face) {
  const int32_t count = int32_t(sizeof(kMethods) / sizeof(kMethods[0]));
  for (int32_t index = 0; index < count; ++index)
    if (!detail::Method2D(state, face, kMethods[index].name,
                          Context2DPathMethod, kMethods[index].length, index)) return false;
  return true;
}
}  // namespace ruyijs::web_canvas
