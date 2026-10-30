#include "web_geometry/matrix.h"
#include "domain/geometry/css_transform.h"
#include "runtime/native_constructor_trace.h"
#include "js/Array.h"
#include "js/ForOfIterator.h"
#include "js/Realm.h"
#include "js/Symbol.h"
#include "js/Wrapper.h"
#include "js/experimental/TypedData.h"
#include <cmath>
#include <string>
#include <vector>

namespace ruyijs::web_geometry {
using domain::geometry::DomMatrix;
const JSClass kMatrixClass = {"DOMMatrix", JSCLASS_HAS_RESERVED_SLOTS(17), nullptr};
const JSClass kMatrixReadOnlyClass = {"DOMMatrixReadOnly", JSCLASS_HAS_RESERVED_SLOTS(17), nullptr};
bool MatrixReceiver(JSContext* cx, JS::HandleValue value, bool readonly, JS::MutableHandleObject out) {
  out.set(value.isObject() ? js::CheckedUnwrapStatic(&value.toObject()) : nullptr);
  const JSClass* clasp = out ? JS::GetClass(out) : nullptr;
  return clasp == &kMatrixClass || (readonly && clasp == &kMatrixReadOnlyClass) ||
         WebKit::Fail(cx, "DOMMatrix member called on an incompatible receiver");
}
void ReadMatrix(JS::HandleObject object, double* values) {
  for (unsigned i = 0; i < 16; ++i) values[i] = JS::GetReservedSlot(object, i).toNumber();
}
DomMatrix LoadMatrix(JS::HandleObject object) {
  double values[16];
  ReadMatrix(object, values);
  return DomMatrix::From(values, JS::GetReservedSlot(object, 16).toBoolean());
}
void StoreMatrix(JSContext* cx, JS::HandleObject object, const DomMatrix& matrix) {
  JSAutoRealm realm(cx, object);
  double values[16];
  matrix.Get(values);
  for (unsigned i = 0; i < 16; ++i) JS::SetReservedSlot(object, i, JS::NumberValue(values[i]));
  JS::SetReservedSlot(object, 16, JS::BooleanValue(matrix.is2D));
}
bool NewMatrix(State& s, bool readonly, const double* values, bool is2D,
                const JS::CallArgs* args, JS::MutableHandleValue result) {
  WebFace& face = readonly ? s.matrixReadOnly : s.matrix;
  JS::RootedObject proto(s.cx, face.prototype);
  if (args && !s.kit.ConstructProto(*args, face.prototype, &proto)) return false;
  JS::RootedObject object(s.cx, s.kit.New(face.clasp, proto));
  if (!object) return false;
  for (unsigned i = 0; i < 16; ++i) JS::SetReservedSlot(object, i, JS::NumberValue(values[i]));
  JS::SetReservedSlot(object, 16, JS::BooleanValue(is2D));
  result.setObject(*object);
  return true;
}
bool NewDomMatrix(State& s, bool readonly, const DomMatrix& matrix, const JS::CallArgs* args,
                  JS::MutableHandleValue result) {
  double values[16];
  matrix.Get(values);
  return NewMatrix(s, readonly, values, matrix.is2D, args, result);
}
bool MatrixDOMException(State& s, const char* name, const char* message) {
  return s.exception ? s.exception(s.exceptionContext, name, message) : WebKit::Fail(s.cx, message);
}

namespace {
bool TypeError(JSContext* cx, const std::string& message) {
  return WebKit::FailKind(cx, message.c_str(), JSEXN_TYPEERR);
}
// DOMMatrixReadOnly SetDataInMatrix for a sequence or typed array.
bool SetData(State& s, const char* prefix, const std::vector<double>& data, DomMatrix* out) {
  const size_t length = data.size();
  out->is2D = length == 6;
  if (length == 16) {
    for (int i = 0; i < 16; ++i) out->SetMember(i, data[i]);
    return true;
  }
  if (length == 6) {
    static constexpr int kMembers[6] = {0, 1, 4, 5, 12, 13};
    for (int i = 0; i < 6; ++i) out->SetMember(kMembers[i], data[i]);
    return true;
  }
  return TypeError(s.cx, std::string(prefix) +
      "Matrix init sequence must have a length of 6 or 16 (actual value: " +
      std::to_string(length) + ")");
}
bool TypedArrayData(State& s, JS::HandleValue value, bool f64, const char* prefix,
                    std::vector<double>* out) {
  JSObject* object = value.isObject() ? js::CheckedUnwrapStatic(&value.toObject()) : nullptr;
  if (!object || (f64 ? !JS_IsFloat64Array(object) : !JS_IsFloat32Array(object)))
    return TypeError(s.cx, std::string(prefix) + "Argument 1 does not implement interface " +
                               (f64 ? "Float64Array." : "Float32Array."));
  const size_t length = JS_GetTypedArrayLength(object);
  bool shared = false;
  JS::AutoCheckCannotGC nogc;
  if (f64) {
    const double* data = JS_GetFloat64ArrayData(object, &shared, nogc);
    out->assign(data, data + length);
  } else {
    const float* data = JS_GetFloat32ArrayData(object, &shared, nogc);
    for (size_t i = 0; i < length; ++i) out->push_back(double(data[i]));
  }
  return true;
}
}  // namespace

// The DOMMatrixInit dictionary (members in IDL order) and
// ValidateAndFixupMatrixInit.
bool ReadMatrixInit(State& s, JS::HandleValue value, const char* prefix, DomMatrix* out) {
  JSContext* cx = s.cx;
  static constexpr const char* k2D[12] = {"a", "b", "c", "d", "e", "f",
                                          "m11", "m12", "m21", "m22", "m41", "m42"};
  static constexpr const char* k3D[10] = {"m13", "m14", "m23", "m24", "m31",
                                          "m32", "m33", "m34", "m43", "m44"};
  double v2[12] = {};
  bool passed2[12] = {};
  double v3[10] = {0, 0, 0, 0, 0, 0, 1, 0, 0, 1};
  bool is2DPassed = false, is2D = false;
  if (!value.isNullOrUndefined()) {
    if (!value.isObject())
      return TypeError(cx, std::string(prefix) + "Value can't be converted to a dictionary.");
    JS::RootedObject object(cx, &value.toObject());
    JS::RootedValue member(cx);
    for (int i = 0; i < 12; ++i) {
      if (!JS_GetProperty(cx, object, k2D[i], &member)) return false;
      if (member.isUndefined()) continue;
      if (!JS::ToNumber(cx, member, &v2[i])) return false;
      passed2[i] = true;
    }
    if (!JS_GetProperty(cx, object, "is2D", &member)) return false;
    if (!member.isUndefined()) {
      is2DPassed = true;
      is2D = JS::ToBoolean(member);
    }
    for (int i = 0; i < 10; ++i) {
      if (!JS_GetProperty(cx, object, k3D[i], &member)) return false;
      if (!member.isUndefined() && !JS::ToNumber(cx, member, &v3[i])) return false;
    }
  }
  // ValidateAndFixupMatrix2DInit: (m11, a) ... (m42, f).
  double m2[6];
  static constexpr double kDefaults[6] = {1, 0, 0, 1, 0, 0};
  for (int i = 0; i < 6; ++i) {
    const int alias = i, field = i + 6;
    if (passed2[field] && passed2[alias]) {
      const double x = v2[field], y = v2[alias];
      if (!(x == y || (std::isnan(x) && std::isnan(y))))
        return TypeError(cx, std::string(prefix) + "Matrix init unexpectedly got different values for '" +
                                 k2D[field] + "' and '" + k2D[alias] + "'.");
    }
    m2[i] = passed2[field] ? v2[field] : passed2[alias] ? v2[alias] : kDefaults[i];
  }
  // Check3DField in Firefox's order: m13 m14 m23 m24 m31 m32 m34 m43 m33 m44.
  static constexpr int kOrder[10] = {0, 1, 2, 3, 4, 5, 7, 8, 6, 9};
  for (int k : kOrder) {
    const double def = (k == 6 || k == 9) ? 1 : 0;
    if (v3[k] != def) {
      if (!is2DPassed) { is2DPassed = true; is2D = false; break; }
      if (is2D)
        return TypeError(cx, std::string(prefix) + "Matrix init has an unexpected 3D element '" +
                                 k3D[k] + "' which cannot coexist with 'is2D: true'.");
    }
  }
  if (!is2DPassed) is2D = true;
  DomMatrix matrix;
  matrix.is2D = is2D;
  if (is2D) {
    matrix.m2 = {m2[0], m2[1], m2[2], m2[3], m2[4], m2[5]};
  } else {
    const double all[16] = {m2[0], m2[1], v3[0], v3[1], m2[2], m2[3], v3[2], v3[3],
                            v3[4], v3[5], v3[6], v3[7], m2[4], m2[5], v3[8], v3[9]};
    for (int i = 0; i < 16; ++i) matrix.m4.m[i / 4][i % 4] = all[i];
  }
  *out = matrix;
  return true;
}

bool SetMatrixValue(State& s, JS::HandleString text, DomMatrix* matrix) {
  JS::UniqueChars utf8 = JS_EncodeStringToUTF8(s.cx, text);
  if (!utf8) return false;
  const std::string list(utf8.get());
  if (list.empty()) return true;
  float m[16];
  bool is3D = false;
  if (!domain::geometry::ParseCssTransform(list, m, &is3D))
    return MatrixDOMException(s, "SyntaxError", "An invalid or illegal string was specified");
  if (!is3D) {
    // mMatrix3D dropped, then SetA..SetF on the 2D matrix.
    matrix->is2D = true;
    matrix->m2 = {double(m[0]), double(m[1]), double(m[4]), double(m[5]), double(m[12]),
                  double(m[13])};
  } else {
    matrix->is2D = false;
    for (int i = 0; i < 16; ++i) matrix->m4.m[i / 4][i % 4] = double(m[i]);
  }
  return true;
}

bool MatrixConstruct(JSContext* cx, unsigned argc, JS::Value* vp, bool readonly) {
  auto args = JS::CallArgsFromVp(argc, vp);
  State& s = Self(args);
  NativeConstructorTrace trace(cx, args);
  if (!args.isConstructing()) return WebKit::Fail(cx, "DOMMatrix constructor requires new");
  const char* prefix = readonly ? "DOMMatrixReadOnly constructor: " : "DOMMatrix constructor: ";
  DomMatrix matrix;
  JS::RootedValue init(cx, args.get(0));
  if (!init.isUndefined()) {
    // (UTF8String or sequence<unrestricted double> or DOMMatrixReadOnly).
    JS::RootedObject source(cx);
    bool done = false;
    if (init.isObject()) {
      JSObject* unwrapped = js::CheckedUnwrapStatic(&init.toObject());
      if (unwrapped && (JS::GetClass(unwrapped) == &kMatrixClass ||
                        JS::GetClass(unwrapped) == &kMatrixReadOnlyClass)) {
        source = unwrapped;
        matrix = LoadMatrix(source);
        done = true;
      } else {
        JS::RootedObject object(cx, &init.toObject());
        JS::RootedId key(cx, JS::PropertyKey::Symbol(JS::GetWellKnownSymbol(cx, JS::SymbolCode::iterator)));
        JS::RootedValue iterator(cx);
        if (!JS_GetPropertyById(cx, object, key, &iterator)) return false;
        if (!iterator.isNullOrUndefined()) {
          JS::ForOfIterator iter(cx);
          if (!iter.init(init)) return false;
          std::vector<double> data;
          for (;;) {
            bool finished;
            JS::RootedValue item(cx);
            if (!iter.next(&item, &finished)) return false;
            if (finished) break;
            double number;
            if (!JS::ToNumber(cx, item, &number)) return false;
            if (data.size() > (1u << 20)) return WebKit::Fail(cx, "DOMMatrix init sequence is too long");
            data.push_back(number);
          }
          if (!SetData(s, prefix, data, &matrix)) return false;
          done = true;
        }
      }
    }
    if (!done) {
      JS::RootedString text(cx, JS::ToString(cx, init));
      if (!text) return false;
      if (s.worker) return TypeError(cx, std::string(prefix) + "Illegal constructor.");
      if (!SetMatrixValue(s, text, &matrix)) return false;
    }
  }
  return trace.Finish(NewDomMatrix(s, readonly, matrix, &args, args.rval()));
}

// fromMatrix (slot 0 / 1), fromFloat32Array (2 / 3), fromFloat64Array (4 / 5):
// even slots build a DOMMatrixReadOnly.
bool MatrixFrom(JSContext* cx, unsigned argc, JS::Value* vp) {
  auto args = JS::CallArgsFromVp(argc, vp);
  State& s = Self(args);
  const int slot = WebKit::Slot(args);
  const bool readonly = slot % 2 == 0;
  const int kind = slot / 2;
  const char* names[3][2] = {{"DOMMatrixReadOnly.fromMatrix: ", "DOMMatrix.fromMatrix: "},
                             {"DOMMatrixReadOnly.fromFloat32Array: ", "DOMMatrix.fromFloat32Array: "},
                             {"DOMMatrixReadOnly.fromFloat64Array: ", "DOMMatrix.fromFloat64Array: "}};
  const char* prefix = names[kind][readonly ? 0 : 1];
  DomMatrix matrix;
  if (kind == 0) {
    if (!ReadMatrixInit(s, args.get(0), prefix, &matrix)) return false;
  } else {
    if (argc < 1) return TypeError(cx, std::string(prefix) + "At least 1 argument required, but only 0 passed");
    std::vector<double> data;
    if (!TypedArrayData(s, args[0], kind == 2, prefix, &data)) return false;
    if (!SetData(s, prefix, data, &matrix)) return false;
  }
  return NewDomMatrix(s, readonly, matrix, nullptr, args.rval());
}

bool Realm::MatrixFrom2D(const double components[6], JS::MutableHandleObject result) {
  State& s = *state_;
  JS::RootedValue value(s.cx);
  {
    JSAutoRealm realm(s.cx, s.kit.page);
    double values[16]; domain::geometry::MatrixIdentity(values);
    for (unsigned i = 0; i < 6; ++i) values[domain::geometry::kAliases[i]] = components[i];
    if (!NewMatrix(s, false, values, true, nullptr, &value)) return false;
  }
  if (!JS_WrapValue(s.cx, &value)) return false;
  result.set(&value.toObject());
  return true;
}
}
