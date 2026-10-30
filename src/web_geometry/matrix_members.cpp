#include "web_geometry/matrix.h"
#include "js/Realm.h"

namespace ruyijs::web_geometry {
using domain::geometry::DomMatrix;

bool MatrixGet(JSContext* cx, unsigned argc, JS::Value* vp) {
  auto args = JS::CallArgsFromVp(argc, vp);
  unsigned slot = WebKit::Slot(args), field = slot % 32;
  JS::RootedObject object(cx);
  if (!MatrixReceiver(cx, args.thisv(), slot < 32, &object)) return false;
  if (field < 22) args.rval().set(JS::GetReservedSlot(object, MatrixIndex(field)));
  else if (field == 22) args.rval().set(JS::GetReservedSlot(object, 16));
  else args.rval().setBoolean(LoadMatrix(object).IsIdentity());
  return true;
}

// Set2DMatrixMember / Set3DMatrixMember.
bool MatrixSet(JSContext* cx, unsigned argc, JS::Value* vp) {
  auto args = JS::CallArgsFromVp(argc, vp);
  unsigned index = MatrixIndex(WebKit::Slot(args) % 32);
  JS::RootedObject object(cx);
  if (!MatrixReceiver(cx, args.thisv(), false, &object)) return false;
  if (!argc) return WebKit::Fail(cx, "DOMMatrix setter requires a value");
  double number;
  if (!JS::ToNumber(cx, args[0], &number)) return false;
  DomMatrix matrix = LoadMatrix(object);
  matrix.SetMember(int(index), number);
  StoreMatrix(cx, object, matrix);
  args.rval().setUndefined();
  return true;
}

namespace {
// The WebIDL arguments: each optional unrestricted double is converted in
// order; undefined takes the default (or stays "not passed").
struct Numbers {
  double value[6] = {};
  bool passed[6] = {};
};
bool ReadNumbers(JSContext* cx, const JS::CallArgs& args, unsigned count,
                 const double* defaults, Numbers* out) {
  for (unsigned i = 0; i < count; ++i) {
    out->value[i] = defaults[i];
    if (i < args.length() && !args[i].isUndefined()) {
      if (!JS::ToNumber(cx, args[i], &out->value[i])) return false;
      out->passed[i] = true;
    }
  }
  return true;
}

const char* MethodPrefix(MatrixOperation op, bool mutableReceiver) {
  switch (op) {
    case OpMultiply: return mutableReceiver ? "DOMMatrix.multiply: " : "DOMMatrixReadOnly.multiply: ";
    case OpMultiplySelf: return "DOMMatrix.multiplySelf: ";
    case OpPreMultiplySelf: return "DOMMatrix.preMultiplySelf: ";
    default: return "DOMMatrix: ";
  }
}

// The operation itself on Firefox's storage.
bool Apply(State& s, const JS::CallArgs& args, MatrixOperation op, bool mutableReceiver,
           DomMatrix* m) {
  JSContext* cx = s.cx;
  Numbers n;
  switch (op) {
    case OpTranslate: case OpTranslateSelf: {
      const double d[3] = {0, 0, 0};
      if (!ReadNumbers(cx, args, 3, d, &n)) return false;
      m->TranslateSelf(n.value[0], n.value[1], n.value[2]);
      return true;
    }
    case OpScale: case OpScaleSelf: {
      const double d[6] = {1, 0, 1, 0, 0, 0};
      if (!ReadNumbers(cx, args, 6, d, &n)) return false;
      m->ScaleSelf(n.value[0], n.passed[1] ? &n.value[1] : nullptr, n.value[2], n.value[3],
                   n.value[4], n.value[5]);
      return true;
    }
    case OpScaleNonUniform: {
      const double d[2] = {1, 1};
      if (!ReadNumbers(cx, args, 2, d, &n)) return false;
      m->ScaleSelf(n.value[0], &n.value[1], 1, 0, 0, 0);
      return true;
    }
    case OpScale3d: case OpScale3dSelf: {
      const double d[4] = {1, 0, 0, 0};
      if (!ReadNumbers(cx, args, 4, d, &n)) return false;
      m->Scale3dSelf(n.value[0], n.value[1], n.value[2], n.value[3]);
      return true;
    }
    case OpRotate: case OpRotateSelf: {
      const double d[3] = {0, 0, 0};
      if (!ReadNumbers(cx, args, 3, d, &n)) return false;
      m->RotateSelf(n.value[0], n.passed[1] ? &n.value[1] : nullptr,
                    n.passed[2] ? &n.value[2] : nullptr);
      return true;
    }
    case OpRotateFromVector: case OpRotateFromVectorSelf: {
      const double d[2] = {0, 0};
      if (!ReadNumbers(cx, args, 2, d, &n)) return false;
      m->RotateFromVectorSelf(n.value[0], n.value[1]);
      return true;
    }
    case OpRotateAxisAngle: case OpRotateAxisAngleSelf: {
      const double d[4] = {0, 0, 0, 0};
      if (!ReadNumbers(cx, args, 4, d, &n)) return false;
      m->RotateAxisAngleSelf(n.value[0], n.value[1], n.value[2], n.value[3]);
      return true;
    }
    case OpSkewX: case OpSkewXSelf: case OpSkewY: case OpSkewYSelf: {
      const double d[1] = {0};
      if (!ReadNumbers(cx, args, 1, d, &n)) return false;
      if (op == OpSkewX || op == OpSkewXSelf) m->SkewXSelf(n.value[0]);
      else m->SkewYSelf(n.value[0]);
      return true;
    }
    case OpMultiply: case OpMultiplySelf: case OpPreMultiplySelf: {
      DomMatrix other;
      if (!ReadMatrixInit(s, args.get(0), MethodPrefix(op, mutableReceiver), &other)) return false;
      if (op == OpPreMultiplySelf) m->PreMultiplySelf(other);
      else m->MultiplySelf(other);
      return true;
    }
    case OpFlipX: m->Flip(false); return true;
    case OpFlipY: m->Flip(true); return true;
    case OpInverse: case OpInvertSelf: m->InvertSelf(); return true;
    default: return true;
  }
}

// DOMMatrixReadOnly.transformPoint(optional DOMPointInit point = {}).
bool TransformPoint(State& s, JS::HandleObject object, const JS::CallArgs& args) {
  JSContext* cx = s.cx;
  double point[4] = {0, 0, 0, 1};  // x y z w
  JS::RootedValue init(cx, args.get(0));
  if (!init.isNullOrUndefined()) {
    if (!init.isObject())
      return WebKit::FailKind(cx, "DOMMatrixReadOnly.transformPoint: Value can't be converted to a dictionary.", JSEXN_TYPEERR);
    JS::RootedObject dict(cx, &init.toObject());
    JS::RootedValue member(cx);
    // DOMPointInit members in IDL order: w, x, y, z.
    static constexpr const char* kNames[4] = {"w", "x", "y", "z"};
    static constexpr int kIndex[4] = {3, 0, 1, 2};
    for (int i = 0; i < 4; ++i) {
      if (!JS_GetProperty(cx, dict, kNames[i], &member)) return false;
      if (!member.isUndefined() && !JS::ToNumber(cx, member, &point[kIndex[i]])) return false;
    }
  }
  double out[4];
  LoadMatrix(object).TransformPoint(point, out);
  return NewValue(s, Point, out, nullptr, args.rval());
}
}  // namespace

bool MatrixMethod(JSContext* cx, unsigned argc, JS::Value* vp) {
  auto args = JS::CallArgsFromVp(argc, vp);
  State& s = Self(args);
  const auto op = MatrixOperation(WebKit::Slot(args));
  const bool self = op >= OpMultiplySelf;
  JS::RootedObject object(cx);
  if (!MatrixReceiver(cx, args.thisv(), !self, &object)) return false;
  if (op == MatrixPoint) return TransformPoint(s, object, args);
  if (op == MatrixFloat32 || op == MatrixFloat64 || op == MatrixJSON || op == MatrixString)
    return MatrixExport(s, object, op, args.rval());
  if (op == SetMatrixString) {
    if (!argc)
      return WebKit::FailKind(cx, "DOMMatrix.setMatrixValue: At least 1 argument required, but only 0 passed", JSEXN_TYPEERR);
    JS::RootedString text(cx, JS::ToString(cx, args[0]));
    if (!text) return false;
    DomMatrix matrix = LoadMatrix(object);
    if (!SetMatrixValue(s, text, &matrix)) return false;
    StoreMatrix(cx, object, matrix);
    args.rval().set(args.thisv());
    return true;
  }
  DomMatrix matrix = LoadMatrix(object);
  const bool mutableReceiver = JS::GetClass(object) == &kMatrixClass;
  if (!Apply(s, args, op, mutableReceiver, &matrix)) return false;
  if (self) {
    StoreMatrix(cx, object, matrix);
    args.rval().set(args.thisv());
    return true;
  }
  // `new DOMMatrix(mParent, *this)` then the Self operation: always a DOMMatrix.
  return NewDomMatrix(s, false, matrix, nullptr, args.rval());
}
}
