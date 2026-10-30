#include "web_geometry/matrix.h"
#include "js/Conversions.h"
#include "js/experimental/TypedData.h"
#include <cmath>
#include <string>

namespace ruyijs::web_geometry {
bool MatrixExport(State& s, JS::HandleObject object, MatrixOperation op, JS::MutableHandleValue result) {
  double values[16]; ReadMatrix(object, values);
  bool is2D = JS::GetReservedSlot(object, 16).toBoolean();
  if (op == MatrixJSON) {
    JS::RootedObject json(s.cx, JS_NewPlainObject(s.cx));
    if (!json) return false;
    for (unsigned i = 0; i < 22; ++i)
      if (!JS_DefineProperty(s.cx, json, kMatrixFields[i], values[MatrixIndex(i)], JSPROP_ENUMERATE)) return false;
    if (!JS_DefineProperty(s.cx, json, "is2D", is2D, JSPROP_ENUMERATE) ||
        !JS_DefineProperty(s.cx, json, "isIdentity", domain::geometry::MatrixIsIdentity(values), JSPROP_ENUMERATE)) return false;
    result.setObject(*json);
    return true;
  }
  if (op == MatrixFloat32 || op == MatrixFloat64) {
    JS::RootedObject array(s.cx, op == MatrixFloat32 ? JS_NewFloat32Array(s.cx, 16) : JS_NewFloat64Array(s.cx, 16));
    if (!array) return false;
    for (unsigned i = 0; i < 16; ++i)
      if (!JS_SetElement(s.cx, array, i, values[i])) return false;
    result.setObject(*array);
    return true;
  }
  // DOMMatrixReadOnly::Stringify: JS::NumberToString of each member; a
  // non-finite member is an InvalidStateError.
  std::string out = is2D ? "matrix(" : "matrix3d(";
  for (unsigned i = 0; i < (is2D ? 6u : 16u); ++i) {
    const double number = values[is2D ? domain::geometry::kAliases[i] : i];
    if (!std::isfinite(number))
      return MatrixDOMException(s, "InvalidStateError",
                                "DOMMatrixReadOnly.toString: Matrix with a non-finite element "
                                "cannot be stringified.");
    char buffer[JS::MaximumNumberToStringLength];
    JS::NumberToString(number, buffer);
    if (i) out += ", ";
    out += buffer;
  }
  out += ')';
  JS::RootedString text(s.cx, JS_NewStringCopyN(s.cx, out.data(), out.size()));
  if (!text) return false;
  result.setString(text);
  return true;
}
}
