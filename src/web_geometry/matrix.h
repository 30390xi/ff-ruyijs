#pragma once
#include "web_geometry/internal.h"
#include "domain/geometry/matrix.h"
#include "domain/geometry/dom_matrix.h"
namespace ruyijs::web_geometry {
extern const JSClass kMatrixClass, kMatrixReadOnlyClass;
inline constexpr const char* kMatrixFields[] = {"a", "b", "c", "d", "e", "f",
  "m11", "m12", "m13", "m14", "m21", "m22", "m23", "m24",
  "m31", "m32", "m33", "m34", "m41", "m42", "m43", "m44", "is2D", "isIdentity"};
inline unsigned MatrixIndex(unsigned field) {
  return field < 6 ? domain::geometry::kAliases[field] : field - 6;
}
// The method slot of every DOMMatrixReadOnly / DOMMatrix member function.
enum MatrixOperation {
  // DOMMatrixReadOnly: a new DOMMatrix from a copy of the receiver.
  OpTranslate, OpScale, OpScaleNonUniform, OpScale3d, OpRotate, OpRotateFromVector,
  OpRotateAxisAngle, OpSkewX, OpSkewY, OpMultiply, OpFlipX, OpFlipY, OpInverse,
  // DOMMatrixReadOnly: exports.
  MatrixFloat32, MatrixFloat64, MatrixPoint, MatrixJSON, MatrixString,
  // DOMMatrix: in place, returning the receiver.
  OpMultiplySelf, OpPreMultiplySelf, OpTranslateSelf, OpScaleSelf, OpScale3dSelf, OpRotateSelf,
  OpRotateFromVectorSelf, OpRotateAxisAngleSelf, OpSkewXSelf, OpSkewYSelf, OpInvertSelf,
  SetMatrixString,
};
bool MatrixReceiver(JSContext*, JS::HandleValue, bool readonly, JS::MutableHandleObject);
void ReadMatrix(JS::HandleObject, double*);
// The receiver's storage as Firefox's mMatrix2D / mMatrix3D.
domain::geometry::DomMatrix LoadMatrix(JS::HandleObject);
void StoreMatrix(JSContext*, JS::HandleObject, const domain::geometry::DomMatrix&);
bool NewMatrix(State&, bool readonly, const double*, bool is2D,
                const JS::CallArgs*, JS::MutableHandleValue);
bool NewDomMatrix(State&, bool readonly, const domain::geometry::DomMatrix&,
                  const JS::CallArgs*, JS::MutableHandleValue);
// DOMMatrixInit conversion + "validate and fixup"; prefix starts the messages.
bool ReadMatrixInit(State&, JS::HandleValue, const char* prefix, domain::geometry::DomMatrix*);
// DOMMatrixReadOnly::SetMatrixValue; false with a pending exception.
bool SetMatrixValue(State&, JS::HandleString, domain::geometry::DomMatrix*);
// Throws a DOMException of the given name.
bool MatrixDOMException(State&, const char* name, const char* message);
bool MatrixConstruct(JSContext*, unsigned, JS::Value*, bool readonly);
bool MatrixGet(JSContext*, unsigned, JS::Value*);
bool MatrixSet(JSContext*, unsigned, JS::Value*);
bool MatrixFrom(JSContext*, unsigned, JS::Value*);
bool MatrixMethod(JSContext*, unsigned, JS::Value*);
bool MatrixExport(State&, JS::HandleObject, MatrixOperation, JS::MutableHandleValue);
bool InstallMatrix(State&);
}
