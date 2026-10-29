// DOMMatrix arithmetic as Firefox 159 does it (dom/base/DOMMatrix.cpp over
// gfx::MatrixDouble and gfx::Matrix4x4Double). A 2D matrix keeps only its six
// BaseMatrix<double> members and runs the 2D formulas; a 3D matrix runs the
// 4x4 ones. Every expression keeps Firefox's operand order so results are the
// same doubles.
#pragma once
#include <cfloat>
#include <cmath>
#include <limits>

namespace ruyijs::domain::geometry {

// gfx/2d/Matrix.cpp FlushToZero.
inline double FlushToZero(double value) noexcept {
  if (-FLT_EPSILON < value && value < FLT_EPSILON) return 0.0f;
  return value;
}

inline constexpr double kRadPerDegree = 2.0 * 3.14159265358979323846 / 360.0;

// gfx::BaseMatrix<double>.
struct Matrix2D {
  double _11 = 1, _12 = 0, _21 = 0, _22 = 1, _31 = 0, _32 = 0;
  Matrix2D operator*(const Matrix2D& m) const noexcept {
    Matrix2D r;
    r._11 = _11 * m._11 + _12 * m._21;
    r._12 = _11 * m._12 + _12 * m._22;
    r._21 = _21 * m._11 + _22 * m._21;
    r._22 = _21 * m._12 + _22 * m._22;
    r._31 = _31 * m._11 + _32 * m._21 + m._31;
    r._32 = _31 * m._12 + _32 * m._22 + m._32;
    return r;
  }
  void PreTranslate(double x, double y) noexcept {
    _31 += _11 * x + _21 * y;
    _32 += _12 * x + _22 * y;
  }
  static Matrix2D Rotation(double angle) noexcept {
    Matrix2D m;
    const double s = std::sin(angle), c = std::cos(angle);
    m._11 = c;
    m._12 = s;
    m._21 = -s;
    m._22 = c;
    return m;
  }
  bool Invert() noexcept {
    const double A = _22, B = -_21, C = _21 * _32 - _22 * _31;
    const double D = -_12, E = _11, F = _31 * _12 - _11 * _32;
    const double det = _11 * _22 - _12 * _21;
    if (!det) return false;
    const double inv = 1 / det;
    _11 = inv * A;
    _12 = inv * D;
    _21 = inv * B;
    _22 = inv * E;
    _31 = inv * C;
    _32 = inv * F;
    return true;
  }
  bool IsIdentity() const noexcept {
    return _11 == 1.0f && _12 == 0.0f && _21 == 0.0f && _22 == 1.0f && _31 == 0.0f &&
           _32 == 0.0f;
  }
};

// gfx::Matrix4x4Double; m[r][c] is _(r+1)(c+1).
struct Matrix4D {
  double m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
  double& at(int r, int c) noexcept { return m[r - 1][c - 1]; }
  double at(int r, int c) const noexcept { return m[r - 1][c - 1]; }

  static Matrix4D From2D(const Matrix2D& a) noexcept {
    Matrix4D r;
    r.at(1, 1) = a._11;
    r.at(1, 2) = a._12;
    r.at(2, 1) = a._21;
    r.at(2, 2) = a._22;
    r.at(4, 1) = a._31;
    r.at(4, 2) = a._32;
    return r;
  }
  Matrix4D operator*(const Matrix4D& b) const noexcept {
    Matrix4D r;
    for (int i = 1; i <= 4; ++i)
      for (int j = 1; j <= 4; ++j)
        r.at(i, j) = at(i, 1) * b.at(1, j) + at(i, 2) * b.at(2, j) + at(i, 3) * b.at(3, j) +
                     at(i, 4) * b.at(4, j);
    return r;
  }
  void PreTranslate(double x, double y, double z) noexcept {
    for (int c = 1; c <= 4; ++c) at(4, c) += x * at(1, c) + y * at(2, c) + z * at(3, c);
  }
  // RotateX/Y/Z: rows a and b mixed by the flushed cos/sin.
  void RotateRows(int a, int b, double theta, bool negateFirst) noexcept {
    const double cosTheta = FlushToZero(std::cos(theta));
    const double sinTheta = FlushToZero(std::sin(theta));
    for (int c = 1; c <= 4; ++c) {
      const double temp = at(a, c);
      if (!negateFirst) {
        at(a, c) = cosTheta * at(a, c) + sinTheta * at(b, c);
        at(b, c) = -sinTheta * temp + cosTheta * at(b, c);
      } else {
        at(a, c) = cosTheta * at(a, c) + -sinTheta * at(b, c);
        at(b, c) = sinTheta * temp + cosTheta * at(b, c);
      }
    }
  }
  void RotateX(double theta) noexcept { RotateRows(2, 3, theta, false); }
  void RotateY(double theta) noexcept { RotateRows(1, 3, theta, true); }
  void RotateZ(double theta) noexcept { RotateRows(1, 2, theta, false); }
  void SetRotateAxisAngle(double ax, double ay, double az, double theta) noexcept {
    double x = ax, y = ay, z = az;
    double length = std::sqrt(x * x + y * y + z * z);
    if (!length) return;
    // BasePoint3D::RobustNormalize.
    if (std::isinf(length)) {
      const double max = std::numeric_limits<double>::max();
      x /= max;
      y /= max;
      z /= max;
      length = std::sqrt(x * x + y * y + z * z);
    }
    x /= length;
    y /= length;
    z /= length;
    const double cosTheta = FlushToZero(std::cos(theta));
    const double sinTheta = FlushToZero(std::sin(theta));
    const double sc = sinTheta / 2;
    const double sq = (1 - cosTheta) / 2;
    at(1, 1) = 1 - 2 * (y * y + z * z) * sq;
    at(1, 2) = 2 * (x * y * sq + z * sc);
    at(1, 3) = 2 * (x * z * sq - y * sc);
    at(1, 4) = 0.0f;
    at(2, 1) = 2 * (x * y * sq - z * sc);
    at(2, 2) = 1 - 2 * (x * x + z * z) * sq;
    at(2, 3) = 2 * (y * z * sq + x * sc);
    at(2, 4) = 0.0f;
    at(3, 1) = 2 * (x * z * sq + y * sc);
    at(3, 2) = 2 * (y * z * sq - x * sc);
    at(3, 3) = 1 - 2 * (x * x + y * y) * sq;
    at(3, 4) = 0.0f;
    at(4, 1) = 0.0f;
    at(4, 2) = 0.0f;
    at(4, 3) = 0.0f;
    at(4, 4) = 1.0f;
  }
  double Determinant() const noexcept {
    const double _11 = at(1, 1), _12 = at(1, 2), _13 = at(1, 3), _14 = at(1, 4);
    const double _21 = at(2, 1), _22 = at(2, 2), _23 = at(2, 3), _24 = at(2, 4);
    const double _31 = at(3, 1), _32 = at(3, 2), _33 = at(3, 3), _34 = at(3, 4);
    const double _41 = at(4, 1), _42 = at(4, 2), _43 = at(4, 3), _44 = at(4, 4);
    return _14 * _23 * _32 * _41 - _13 * _24 * _32 * _41 - _14 * _22 * _33 * _41 +
           _12 * _24 * _33 * _41 + _13 * _22 * _34 * _41 - _12 * _23 * _34 * _41 -
           _14 * _23 * _31 * _42 + _13 * _24 * _31 * _42 + _14 * _21 * _33 * _42 -
           _11 * _24 * _33 * _42 - _13 * _21 * _34 * _42 + _11 * _23 * _34 * _42 +
           _14 * _22 * _31 * _43 - _12 * _24 * _31 * _43 - _14 * _21 * _32 * _43 +
           _11 * _24 * _32 * _43 + _12 * _21 * _34 * _43 - _11 * _22 * _34 * _43 -
           _13 * _22 * _31 * _44 + _12 * _23 * _31 * _44 + _13 * _21 * _32 * _44 -
           _11 * _23 * _32 * _44 - _12 * _21 * _33 * _44 + _11 * _22 * _33 * _44;
  }
  bool Invert() noexcept {
    const double det = Determinant();
    if (!det) return false;
    const double _11 = at(1, 1), _12 = at(1, 2), _13 = at(1, 3), _14 = at(1, 4);
    const double _21 = at(2, 1), _22 = at(2, 2), _23 = at(2, 3), _24 = at(2, 4);
    const double _31 = at(3, 1), _32 = at(3, 2), _33 = at(3, 3), _34 = at(3, 4);
    const double _41 = at(4, 1), _42 = at(4, 2), _43 = at(4, 3), _44 = at(4, 4);
    Matrix4D r;
    r.at(1, 1) = _23 * _34 * _42 - _24 * _33 * _42 + _24 * _32 * _43 - _22 * _34 * _43 -
                 _23 * _32 * _44 + _22 * _33 * _44;
    r.at(1, 2) = _14 * _33 * _42 - _13 * _34 * _42 - _14 * _32 * _43 + _12 * _34 * _43 +
                 _13 * _32 * _44 - _12 * _33 * _44;
    r.at(1, 3) = _13 * _24 * _42 - _14 * _23 * _42 + _14 * _22 * _43 - _12 * _24 * _43 -
                 _13 * _22 * _44 + _12 * _23 * _44;
    r.at(1, 4) = _14 * _23 * _32 - _13 * _24 * _32 - _14 * _22 * _33 + _12 * _24 * _33 +
                 _13 * _22 * _34 - _12 * _23 * _34;
    r.at(2, 1) = _24 * _33 * _41 - _23 * _34 * _41 - _24 * _31 * _43 + _21 * _34 * _43 +
                 _23 * _31 * _44 - _21 * _33 * _44;
    r.at(2, 2) = _13 * _34 * _41 - _14 * _33 * _41 + _14 * _31 * _43 - _11 * _34 * _43 -
                 _13 * _31 * _44 + _11 * _33 * _44;
    r.at(2, 3) = _14 * _23 * _41 - _13 * _24 * _41 - _14 * _21 * _43 + _11 * _24 * _43 +
                 _13 * _21 * _44 - _11 * _23 * _44;
    r.at(2, 4) = _13 * _24 * _31 - _14 * _23 * _31 + _14 * _21 * _33 - _11 * _24 * _33 -
                 _13 * _21 * _34 + _11 * _23 * _34;
    r.at(3, 1) = _22 * _34 * _41 - _24 * _32 * _41 + _24 * _31 * _42 - _21 * _34 * _42 -
                 _22 * _31 * _44 + _21 * _32 * _44;
    r.at(3, 2) = _14 * _32 * _41 - _12 * _34 * _41 - _14 * _31 * _42 + _11 * _34 * _42 +
                 _12 * _31 * _44 - _11 * _32 * _44;
    r.at(3, 3) = _12 * _24 * _41 - _14 * _22 * _41 + _14 * _21 * _42 - _11 * _24 * _42 -
                 _12 * _21 * _44 + _11 * _22 * _44;
    r.at(3, 4) = _14 * _22 * _31 - _12 * _24 * _31 - _14 * _21 * _32 + _11 * _24 * _32 +
                 _12 * _21 * _34 - _11 * _22 * _34;
    r.at(4, 1) = _23 * _32 * _41 - _22 * _33 * _41 - _23 * _31 * _42 + _21 * _33 * _42 +
                 _22 * _31 * _43 - _21 * _32 * _43;
    r.at(4, 2) = _12 * _33 * _41 - _13 * _32 * _41 + _13 * _31 * _42 - _11 * _33 * _42 -
                 _12 * _31 * _43 + _11 * _32 * _43;
    r.at(4, 3) = _13 * _22 * _41 - _12 * _23 * _41 - _13 * _21 * _42 + _11 * _23 * _42 +
                 _12 * _21 * _43 - _11 * _22 * _43;
    r.at(4, 4) = _12 * _23 * _31 - _13 * _22 * _31 + _13 * _21 * _32 - _11 * _23 * _32 -
                 _12 * _21 * _33 + _11 * _22 * _33;
    // Row-major order of the per-member divisions.
    for (int i = 1; i <= 4; ++i)
      for (int j = 1; j <= 4; ++j) r.at(i, j) /= det;
    *this = r;
    return true;
  }
  void SetNaN() noexcept {
    for (auto& row : m)
      for (double& v : row) v = std::numeric_limits<double>::quiet_NaN();
  }
  bool IsIdentity() const noexcept {
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j)
        if (m[i][j] != (i == j ? 1.0f : 0.0f)) return false;
    return true;
  }
};

// DOMMatrixReadOnly's storage: mMatrix2D when is2D, otherwise mMatrix3D.
struct DomMatrix {
  bool is2D = true;
  Matrix2D m2;
  Matrix4D m4;

  // The sixteen members in m11..m44 order (M11() ... M44()).
  void Get(double out[16]) const noexcept {
    if (!is2D) {
      for (int i = 0; i < 16; ++i) out[i] = m4.m[i / 4][i % 4];
      return;
    }
    Matrix4D identity;
    for (int i = 0; i < 16; ++i) out[i] = identity.m[i / 4][i % 4];
    out[0] = m2._11;
    out[1] = m2._12;
    out[4] = m2._21;
    out[5] = m2._22;
    out[12] = m2._31;
    out[13] = m2._32;
  }
  static DomMatrix From(const double values[16], bool is2D) noexcept {
    DomMatrix r;
    r.is2D = is2D;
    if (is2D) {
      r.m2._11 = values[0];
      r.m2._12 = values[1];
      r.m2._21 = values[4];
      r.m2._22 = values[5];
      r.m2._31 = values[12];
      r.m2._32 = values[13];
    } else {
      for (int i = 0; i < 16; ++i) r.m4.m[i / 4][i % 4] = values[i];
    }
    return r;
  }
  bool IsIdentity() const noexcept { return is2D ? m2.IsIdentity() : m4.IsIdentity(); }
  void Ensure3D() noexcept {
    if (is2D) {
      m4 = Matrix4D::From2D(m2);
      is2D = false;
    }
  }
  // Set2DMatrixMember / Set3DMatrixMember for member index 0..15.
  void SetMember(int index, double v) noexcept {
    static constexpr int k2D[16] = {0, 1, -1, -1, 2, 3, -1, -1, -1, -1, -1, -1, 4, 5, -1, -1};
    const int slot = k2D[index];
    if (slot >= 0) {
      if (!is2D) {
        m4.m[index / 4][index % 4] = v;
        return;
      }
      double* fields[6] = {&m2._11, &m2._12, &m2._21, &m2._22, &m2._31, &m2._32};
      *fields[slot] = v;
      return;
    }
    const double def = index == 10 || index == 15 ? 1.0 : 0.0;
    if (!is2D || v != def) {
      Ensure3D();
      m4.m[index / 4][index % 4] = v;
    }
  }

  void TranslateSelf(double tx, double ty, double tz) noexcept {
    if (tx == 0 && ty == 0 && tz == 0) return;
    if (!is2D || tz != 0) {
      Ensure3D();
      m4.PreTranslate(tx, ty, tz);
    } else {
      m2.PreTranslate(tx, ty);
    }
  }
  void ScaleSelf(double sx, const double* syOptional, double sz, double ox, double oy,
                 double oz) noexcept {
    const double sy = syOptional ? *syOptional : sx;
    TranslateSelf(ox, oy, oz);
    if (!is2D || sz != 1.0) {
      Ensure3D();
      Matrix4D m;
      m.at(1, 1) = sx;
      m.at(2, 2) = sy;
      m.at(3, 3) = sz;
      m4 = m * m4;
    } else {
      Matrix2D m;
      m._11 = sx;
      m._22 = sy;
      m2 = m * m2;
    }
    TranslateSelf(-ox, -oy, -oz);
  }
  void Scale3dSelf(double s, double ox, double oy, double oz) noexcept {
    ScaleSelf(s, &s, s, ox, oy, oz);
  }
  void RotateAxisAngleSelf(double x, double y, double z, double angle) noexcept {
    if (std::fmod(angle, 360) == 0) return;
    angle *= kRadPerDegree;
    Matrix4D m;
    m.SetRotateAxisAngle(x, y, z, angle);
    if (!is2D || x != 0 || y != 0) {
      Ensure3D();
      m4 = m * m4;
      return;
    }
    const Matrix4D result = m * Matrix4D::From2D(m2);
    m2._11 = result.at(1, 1);
    m2._12 = result.at(1, 2);
    m2._21 = result.at(2, 1);
    m2._22 = result.at(2, 2);
    m2._31 = result.at(4, 1);
    m2._32 = result.at(4, 2);
  }
  void RotateFromVectorSelf(double x, double y) noexcept {
    const double angle = (x == 0.0 && y == 0.0) ? 0 : std::atan2(y, x);
    if (std::fmod(angle, 2 * 3.14159265358979323846) == 0) return;
    if (!is2D) RotateAxisAngleSelf(0, 0, 1, angle / kRadPerDegree);
    else m2 = Matrix2D::Rotation(angle) * m2;
  }
  void RotateSelf(double rotX, const double* rotYOptional, const double* rotZOptional) noexcept {
    double rotY, rotZ;
    if (!rotYOptional && !rotZOptional) {
      rotZ = rotX;
      rotX = 0;
      rotY = 0;
    } else {
      rotY = rotYOptional ? *rotYOptional : 0;
      rotZ = rotZOptional ? *rotZOptional : 0;
    }
    if (rotX != 0 || rotY != 0) Ensure3D();
    if (!is2D) {
      if (std::fmod(rotZ, 360) != 0) m4.RotateZ(rotZ * kRadPerDegree);
      if (std::fmod(rotY, 360) != 0) m4.RotateY(rotY * kRadPerDegree);
      if (std::fmod(rotX, 360) != 0) m4.RotateX(rotX * kRadPerDegree);
    } else if (std::fmod(rotZ, 360) != 0) {
      m2 = Matrix2D::Rotation(rotZ * kRadPerDegree) * m2;
    }
  }
  void SkewXSelf(double sx) noexcept {
    if (std::fmod(sx, 360) == 0) return;
    if (!is2D) {
      Matrix4D m;
      m.at(2, 1) = std::tan(sx * kRadPerDegree);
      m4 = m * m4;
    } else {
      Matrix2D m;
      m._21 = std::tan(sx * kRadPerDegree);
      m2 = m * m2;
    }
  }
  void SkewYSelf(double sy) noexcept {
    if (std::fmod(sy, 360) == 0) return;
    if (!is2D) {
      Matrix4D m;
      m.at(1, 2) = std::tan(sy * kRadPerDegree);
      m4 = m * m4;
    } else {
      Matrix2D m;
      m._12 = std::tan(sy * kRadPerDegree);
      m2 = m * m2;
    }
  }
  void MultiplySelf(const DomMatrix& other) noexcept {
    if (other.IsIdentity()) return;
    if (other.is2D) {
      if (!is2D) m4 = Matrix4D::From2D(other.m2) * m4;
      else m2 = other.m2 * m2;
    } else {
      Ensure3D();
      m4 = other.m4 * m4;
    }
  }
  void PreMultiplySelf(const DomMatrix& other) noexcept {
    if (other.IsIdentity()) return;
    if (other.is2D) {
      if (!is2D) m4 = m4 * Matrix4D::From2D(other.m2);
      else m2 = m2 * other.m2;
    } else {
      Ensure3D();
      m4 = m4 * other.m4;
    }
  }
  // DOMMatrixReadOnly::FlipX / FlipY on a copy.
  void Flip(bool y) noexcept {
    if (!is2D) {
      Matrix4D m;
      m.at(y ? 2 : 1, y ? 2 : 1) = -1;
      m4 = m * m4;
    } else {
      Matrix2D m;
      (y ? m._22 : m._11) = -1;
      m2 = m * m2;
    }
  }
  void InvertSelf() noexcept {
    if (!is2D) {
      if (!m4.Invert()) m4.SetNaN();
    } else if (!m2.Invert()) {
      is2D = false;
      m4.SetNaN();
    }
  }
  // DOMMatrixReadOnly::TransformPoint: x y z w in, x y z w out.
  void TransformPoint(const double in[4], double out[4]) const noexcept {
    if (!is2D) {
      // A 3D matrix transforms a gfx::Point4D: float in and out.
      const float p[4] = {float(in[0]), float(in[1]), float(in[2]), float(in[3])};
      for (int c = 1; c <= 4; ++c)
        out[c - 1] = float(p[0] * m4.at(1, c) + p[1] * m4.at(2, c) + p[2] * m4.at(3, c) +
                           p[3] * m4.at(4, c));
      return;
    }
    if (in[2] != 0 || in[3] != 1.0) {
      // gfx::PointDouble4D through From2D.
      const Matrix4D t = Matrix4D::From2D(m2);
      for (int c = 1; c <= 4; ++c)
        out[c - 1] = in[0] * t.at(1, c) + in[1] * t.at(2, c) + in[2] * t.at(3, c) +
                     in[3] * t.at(4, c);
      return;
    }
    out[0] = in[0] * m2._11 + in[1] * m2._21 + m2._31;
    out[1] = in[0] * m2._12 + in[1] * m2._22 + m2._32;
    out[2] = in[2];
    out[3] = in[3];
  }
};
}  // namespace ruyijs::domain::geometry
