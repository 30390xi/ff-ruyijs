#include "web_geometry/matrix.h"
#include "runtime/page_helpers.h"
#include "js/Realm.h"
#include <cstring>

namespace ruyijs::web_geometry {
namespace {
bool ReadOnlyCtor(JSContext* c, unsigned n, JS::Value* p) { return MatrixConstruct(c, n, p, true); }
bool MutableCtor(JSContext* c, unsigned n, JS::Value* p) { return MatrixConstruct(c, n, p, false); }
}
bool InstallMatrix(State& s) {
  JS::RootedObject plain(s.cx, JS::GetRealmObjectPrototype(s.cx));
  if (!s.kit.Define("DOMMatrixReadOnly", &kMatrixReadOnlyClass, plain, ReadOnlyCtor, &s.matrixReadOnly) ||
      !s.kit.Define("DOMMatrix", &kMatrixClass, s.matrixReadOnly.prototype, MutableCtor, &s.matrix) ||
      !JS_SetPrototype(s.cx, s.matrix.constructor, s.matrixReadOnly.constructor) ||
      (!s.worker && !JS_DefineProperty(s.cx, s.kit.page, "WebKitCSSMatrix", s.matrix.constructor, 0))) return false;
  for (unsigned kind = 0; kind < 2; ++kind) {
    WebFace& face = kind ? s.matrix : s.matrixReadOnly;
    const char* name = kind ? "DOMMatrix" : "DOMMatrixReadOnly";
    for (unsigned i = 0; i < (kind ? 22u : 24u); ++i)
      if (!s.kit.Accessor(face, name, kMatrixFields[i], MatrixGet, kind ? MatrixSet : nullptr, kind * 32 + i)) return false;
    // MatrixFrom slot: factory * 2 + (DOMMatrix ? 1 : 0).
    const char* factories[] = {"fromMatrix", "fromFloat32Array", "fromFloat64Array"};
    for (unsigned i = 0; i < 3; ++i) {
      JS::RootedObject fn(s.cx, s.kit.Function(MatrixFrom, factories[i], i ? 1 : 0, int32_t(i * 2 + kind)));
      if (!fn || !JS_DefineProperty(s.cx, face.constructor, factories[i], fn, JSPROP_ENUMERATE) ||
          !s.kit.helpers->Member(fn, name, factories[i], "call")) return false;
    }
  }
  struct Method { const char* name; MatrixOperation op; bool mutableOnly; unsigned length; };
  const Method methods[] = {
    {"flipX", OpFlipX, false, 0}, {"flipY", OpFlipY, false, 0},
    {"inverse", OpInverse, false, 0}, {"multiply", OpMultiply, false, 0},
    {"rotate", OpRotate, false, 0}, {"rotateAxisAngle", OpRotateAxisAngle, false, 0},
    {"rotateFromVector", OpRotateFromVector, false, 0}, {"scale", OpScale, false, 0},
    {"scale3d", OpScale3d, false, 0}, {"scaleNonUniform", OpScaleNonUniform, false, 0},
    {"skewX", OpSkewX, false, 0}, {"skewY", OpSkewY, false, 0},
    {"translate", OpTranslate, false, 0}, {"toFloat32Array", MatrixFloat32, false, 0},
    {"toFloat64Array", MatrixFloat64, false, 0}, {"toJSON", MatrixJSON, false, 0},
    {"toString", MatrixString, false, 0}, {"transformPoint", MatrixPoint, false, 0},
    {"multiplySelf", OpMultiplySelf, true, 0}, {"preMultiplySelf", OpPreMultiplySelf, true, 0},
    {"rotateSelf", OpRotateSelf, true, 0}, {"rotateAxisAngleSelf", OpRotateAxisAngleSelf, true, 0},
    {"rotateFromVectorSelf", OpRotateFromVectorSelf, true, 0}, {"scaleSelf", OpScaleSelf, true, 0},
    {"scale3dSelf", OpScale3dSelf, true, 0}, {"skewXSelf", OpSkewXSelf, true, 0},
    {"skewYSelf", OpSkewYSelf, true, 0}, {"invertSelf", OpInvertSelf, true, 0},
    {"translateSelf", OpTranslateSelf, true, 0}, {"setMatrixValue", SetMatrixString, true, 1}
  };
  for (const auto& method : methods)
    if ((!s.worker || (std::strcmp(method.name, "toString") && std::strcmp(method.name, "setMatrixValue"))) && !s.kit.Method(method.mutableOnly ? s.matrix : s.matrixReadOnly,
                      method.mutableOnly ? "DOMMatrix" : "DOMMatrixReadOnly",
                      method.name, MatrixMethod, method.length, method.op)) return false;
  return true;
}
}
