// The 2D transform members. These are the real matrix math the specification
// defines; there is no need for a rasterizer, so they are implemented in full
// and asserted strictly (A15.4).
#include "web_canvas/internal.h"

#include <cmath>

namespace ruyijs::web_canvas {
namespace {
enum TransformKind : uint8_t { kTranslate, kScale, kRotate, kTransform, kSetTransform,
                               kResetTransform, kGetTransform };

struct MethodSpec {
  const char* name;
  TransformKind kind;
  unsigned length;
};

const MethodSpec kMethods[] = {
    {"translate", kTranslate, 2},       {"scale", kScale, 2},
    {"rotate", kRotate, 1},             {"transform", kTransform, 6},
    {"setTransform", kSetTransform, 0}, {"resetTransform", kResetTransform, 0},
    {"getTransform", kGetTransform, 0},
};

// gfx::Matrix is float: every canvas transform is rounded to float and
// composed in float, so getTransform() and the drawing see Firefox's values.
// Matrix order: [a b c d e f] = [_11 _12 _21 _22 _31 _32].
struct Matrix {
  float m[6];
  bool Finite() const {
    for (float value : m) if (!std::isfinite(value)) return false;
    return true;
  }
};
// BaseMatrix::operator*: lhs applied first.
Matrix Multiply(const Matrix& lhs, const Matrix& rhs) {
  const float* a = lhs.m;
  const float* b = rhs.m;
  return Matrix{{a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3],
                 a[2] * b[0] + a[3] * b[2], a[2] * b[1] + a[3] * b[3],
                 a[4] * b[0] + a[5] * b[2] + b[4], a[4] * b[1] + a[5] * b[3] + b[5]}};
}
Matrix FromDoubles(const double values[6]) {
  return Matrix{{float(values[0]), float(values[1]), float(values[2]), float(values[3]),
                 float(values[4]), float(values[5])}};
}
// SetTransformInternal: a non-finite result leaves the transform unchanged.
bool Commit(JSContext* cx, JS::HandleObject self, const Matrix& matrix) {
  if (!matrix.Finite()) return true;
  double values[6];
  for (int i = 0; i < 6; ++i) values[i] = matrix.m[i];
  return detail::SetMatrix(cx, self, values);
}
}  // namespace

bool Context2DTransformMethod(JSContext* cx, unsigned argc, JS::Value* vp) {
  auto args = JS::CallArgsFromVp(argc, vp);
  const int32_t index = detail::ContextSlot(args);
  JS::RootedObject self(cx);
  if (!detail::ContextReceiver(cx, args, &self)) return false;
  const MethodSpec& spec = kMethods[index];
  State& state = *detail::Self(args);
  args.rval().setUndefined();
  if (spec.kind == kGetTransform) {
    double current[6];
    if (!detail::GetMatrix(cx, self, current)) return false;
    JS::RootedObject matrix(cx);
    if (!state.matrixFactory) return WebKit::Fail(cx, "Canvas matrix factory is not installed");
    if (!state.matrixFactory(state.matrixFactoryContext, current, &matrix)) return false;
    args.rval().setObject(*matrix);
    return true;
  }
  double current[6];
  if (!detail::GetMatrix(cx, self, current)) return false;
  if (spec.kind == kResetTransform) {
    static const double kIdentity[6] = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    return detail::SetMatrix(cx, self, kIdentity);
  }
  double next[6] = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
  if (spec.kind == kSetTransform && argc <= 1) {
    if (!args.get(0).isNullOrUndefined()) {
      if (!args[0].isObject()) return WebKit::Fail(cx,"setTransform requires a dictionary or six numbers");
      JS::RootedObject init(cx,&args[0].toObject());
      const char* names[]={"a","b","c","d","e","f"};
      const char* aliases[]={"m11","m12","m21","m22","m41","m42"};
      for (int i=0;i<6;++i) {
        JS::RootedValue value(cx),alias(cx);
        if (!JS_GetProperty(cx,init,names[i],&value) || !JS_GetProperty(cx,init,aliases[i],&alias)) return false;
        if (!value.isUndefined() && !JS::ToNumber(cx,value,&next[i])) return false;
        if (!alias.isUndefined()) {
          double number;
          if (!JS::ToNumber(cx,alias,&number)) return false;
          if (!value.isUndefined() && next[i]!=number && !(std::isnan(next[i]) && std::isnan(number)))
            return WebKit::Fail(cx,"Conflicting DOMMatrix2DInit aliases");
          next[i]=number;
        }
      }
      for (double number:next) if (!std::isfinite(number)) return true;
    }
  } else if (spec.kind == kTranslate || spec.kind == kScale) {
    double pair[2];
    bool usable = false;
    if (!detail::ReadNumbers(cx, args, 0, 2, pair, &usable) || !usable) return !usable;
    Matrix matrix = FromDoubles(current);
    const float x = float(pair[0]), y = float(pair[1]);
    if (spec.kind == kTranslate) {
      // BaseMatrix::PreTranslate.
      matrix.m[4] += matrix.m[0] * x + matrix.m[2] * y;
      matrix.m[5] += matrix.m[1] * x + matrix.m[3] * y;
    } else {
      // BaseMatrix::PreScale.
      matrix.m[0] *= x;
      matrix.m[1] *= x;
      matrix.m[2] *= y;
      matrix.m[3] *= y;
    }
    return Commit(cx, self, matrix);
  } else if (spec.kind == kRotate) {
    double angle = 0.0;
    bool usable = false;
    if (!detail::ReadNumbers(cx, args, 0, 1, &angle, &usable) || !usable) return !usable;
    // Matrix::Rotation(Float): sinf/cosf of the float angle, then
    // Rotation * current.
    const float radians = float(angle);
    const float sine = sinf(radians), cosine = cosf(radians);
    return Commit(cx, self, Multiply(Matrix{{cosine, sine, -sine, cosine, 0, 0}},
                                     FromDoubles(current)));
  } else {
    bool usable = false;
    if (!detail::ReadNumbers(cx, args, 0, 6, next, &usable) || !usable) return !usable;
  }
  // transform(): Matrix(args) * current; setTransform(): Matrix(args).
  if (spec.kind == kSetTransform) return Commit(cx, self, FromDoubles(next));
  return Commit(cx, self, Multiply(FromDoubles(next), FromDoubles(current)));
}

bool InstallContext2DTransform(State& state, WebFace& face) {
  const int32_t count = int32_t(sizeof(kMethods) / sizeof(kMethods[0]));
  for (int32_t index = 0; index < count; ++index)
    if (!detail::Method2D(state, face, kMethods[index].name,
                          Context2DTransformMethod, kMethods[index].length, index))
      return false;
  return true;
}
}  // namespace ruyijs::web_canvas
