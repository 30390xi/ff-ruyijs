// A CSS <transform-list> to the float 4x4 matrix Firefox's
// Servo_ParseTransformIntoMatrix gives DOMMatrix.setMatrixValue: cssparser's
// number tokens (f64 math, f32 values), absolute lengths and angles as Servo's
// specified values convert them, each function as its euclid Transform3D<f64>
// composed with `then`, and the result clamped and cast to f32. Relative
// lengths and percentages in translations fail, as they do in Firefox; calc()
// is not accepted here.
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace ruyijs::domain::geometry {
namespace css_transform_detail {
// Transform3D<f64>, mRC = m[R-1][C-1].
struct T3 {
  double m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
  // euclid `self.then(other)`.
  T3 Then(const T3& o) const {
    T3 r;
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j)
        r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j] +
                    m[i][3] * o.m[3][j];
    return r;
  }
};

enum class Kind { Number, Percentage, Dimension };
struct Token {
  Kind kind = Kind::Number;
  float value = 0;     // Number / Dimension value (f32)
  float unit_value = 0;  // Percentage value / 100 (f32)
  std::string unit;    // lower-cased
};

class Reader {
 public:
  explicit Reader(const std::string& s) : s_(s) {}
  bool End() const { return i_ >= s_.size(); }
  void SkipSpace() {
    for (;;) {
      if (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' ||
                             s_[i_] == '\r' || s_[i_] == '\f')) {
        ++i_;
      } else if (i_ + 1 < s_.size() && s_[i_] == '/' && s_[i_ + 1] == '*') {
        const size_t close = s_.find("*/", i_ + 2);
        i_ = close == std::string::npos ? s_.size() : close + 2;
      } else {
        return;
      }
    }
  }
  bool Consume(char c) {
    if (i_ < s_.size() && s_[i_] == c) { ++i_; return true; }
    return false;
  }
  static bool NameStart(unsigned char c) {
    return std::isalpha(c) || c == '_' || c >= 0x80;
  }
  static bool NameChar(unsigned char c) {
    return NameStart(c) || std::isdigit(c) || c == '-';
  }
  bool IdentStart() const {
    if (i_ >= s_.size()) return false;
    const unsigned char c = s_[i_];
    if (NameStart(c)) return true;
    if (c == '-' && i_ + 1 < s_.size()) {
      const unsigned char d = s_[i_ + 1];
      return NameStart(d) || d == '-';
    }
    return false;
  }
  std::string Name() {
    std::string out;
    while (i_ < s_.size() && NameChar(static_cast<unsigned char>(s_[i_])))
      out += char(std::tolower(static_cast<unsigned char>(s_[i_++])));
    return out;
  }
  // cssparser consume_numeric, after its "starts a number" check.
  bool Numeric(Token* t) {
    const size_t start = i_;
    double sign = 1.;
    if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) {
      sign = s_[i_] == '-' ? -1. : 1.;
      ++i_;
    }
    const bool digitStart = i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]));
    const bool dotStart = i_ + 1 < s_.size() && s_[i_] == '.' &&
                          std::isdigit(static_cast<unsigned char>(s_[i_ + 1]));
    if (!digitStart && !dotStart) { i_ = start; return false; }
    double integral = 0.;
    while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_])))
      integral = integral * 10. + double(s_[i_++] - '0');
    double fractional = 0.;
    if (i_ + 1 < s_.size() && s_[i_] == '.' && std::isdigit(static_cast<unsigned char>(s_[i_ + 1]))) {
      ++i_;
      double factor = 0.1;
      while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) {
        fractional += double(s_[i_++] - '0') * factor;
        factor *= 0.1;
      }
    }
    double value = sign * (integral + fractional);
    if (i_ + 1 < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E') &&
        (std::isdigit(static_cast<unsigned char>(s_[i_ + 1])) ||
         (i_ + 2 < s_.size() && (s_[i_ + 1] == '+' || s_[i_ + 1] == '-') &&
          std::isdigit(static_cast<unsigned char>(s_[i_ + 2]))))) {
      ++i_;
      double esign = 1.;
      if (s_[i_] == '+' || s_[i_] == '-') { esign = s_[i_] == '-' ? -1. : 1.; ++i_; }
      double exponent = 0.;
      while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_])))
        exponent = exponent * 10. + double(s_[i_++] - '0');
      value *= std::pow(10., esign * exponent);
    }
    if (Consume('%')) {
      t->kind = Kind::Percentage;
      t->unit_value = float(value / 100.);
      return true;
    }
    t->value = float(value);
    if (IdentStart()) {
      t->kind = Kind::Dimension;
      t->unit = Name();
    } else {
      t->kind = Kind::Number;
    }
    return true;
  }
 private:
  const std::string& s_;
  size_t i_ = 0;
};

// specified::Length::to_px_if_absolute (f32).
inline bool AbsolutePx(const Token& t, float* px) {
  if (t.kind == Kind::Number) {
    if (t.value != 0) return false;  // unitless zero only
    *px = 0;
    return true;
  }
  if (t.kind != Kind::Dimension) return false;
  constexpr float kIn = 96.f, kCm = kIn / 2.54f, kMm = kIn / 25.4f, kQ = kMm / 4.f,
                  kPt = kIn / 72.f, kPc = kPt * 12.f;
  float factor;
  if (t.unit == "px") factor = 1.0f;
  else if (t.unit == "in") factor = kIn;
  else if (t.unit == "cm") factor = kCm;
  else if (t.unit == "mm") factor = kMm;
  else if (t.unit == "q") factor = kQ;
  else if (t.unit == "pt") factor = kPt;
  else if (t.unit == "pc") factor = kPc;
  else return false;  // font-, viewport- and container-relative units
  *px = t.value * factor;
  return true;
}

// specified::Angle::parse_with_unitless + radians64.
inline bool Radians(const Token& t, double* radians) {
  float degrees;
  if (t.kind == Kind::Number) {
    if (t.value != 0) return false;
    degrees = 0;
  } else if (t.kind == Kind::Dimension) {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kDegPerRad = 180.0f / kPi, kDegPerTurn = 360.0f, kDegPerGrad = 180.0f / 200.0f;
    if (t.unit == "deg") degrees = t.value;
    else if (t.unit == "rad") degrees = t.value * kDegPerRad;
    else if (t.unit == "turn") degrees = t.value * kDegPerTurn;
    else if (t.unit == "grad") degrees = t.value * kDegPerGrad;
    else return false;
  } else {
    return false;
  }
  *radians = double(degrees) * (3.14159265358979323846 / 180.0);
  return true;
}

inline bool PlainNumber(const Token& t, float* out) {
  if (t.kind != Kind::Number) return false;
  *out = t.value;
  return true;
}
inline bool NumberOrPercentage(const Token& t, float* out) {
  if (t.kind == Kind::Number) { *out = t.value; return true; }
  if (t.kind == Kind::Percentage) { *out = t.unit_value; return true; }
  return false;
}

inline T3 Translation(double x, double y, double z) {
  T3 r;
  r.m[3][0] = x; r.m[3][1] = y; r.m[3][2] = z;
  return r;
}
inline T3 Scale(double x, double y, double z) {
  T3 r;
  r.m[0][0] = x; r.m[1][1] = y; r.m[2][2] = z;
  return r;
}
inline T3 Rotation(double x, double y, double z, double theta) {
  const double xx = x * x, yy = y * y, zz = z * z;
  const double half = theta / 2.0;
  const double sc = std::sin(half) * std::cos(half);
  const double sq = std::sin(half) * std::sin(half);
  T3 r;
  r.m[0][0] = 1.0 - 2.0 * (yy + zz) * sq;
  r.m[0][1] = 2.0 * (x * y * sq + z * sc);
  r.m[0][2] = 2.0 * (x * z * sq - y * sc);
  r.m[1][0] = 2.0 * (x * y * sq - z * sc);
  r.m[1][1] = 1.0 - 2.0 * (xx + zz) * sq;
  r.m[1][2] = 2.0 * (y * z * sq + x * sc);
  r.m[2][0] = 2.0 * (x * z * sq + y * sc);
  r.m[2][1] = 2.0 * (y * z * sq - x * sc);
  r.m[2][2] = 1.0 - 2.0 * (xx + yy) * sq;
  return r;
}
inline T3 Skew(double alpha, double beta) {
  T3 r;
  r.m[0][1] = std::tan(beta);
  r.m[1][0] = std::tan(alpha);
  return r;
}
}  // namespace css_transform_detail

// Returns false where Servo's parse or to_transform_3d_matrix fails.
// out[16] is m11..m44; *is3D tells whether any 3D function appeared.
inline bool ParseCssTransform(const std::string& text, float out[16], bool* is3D) {
  using namespace css_transform_detail;
  Reader r(text);
  T3 acc;
  bool contains3D = false;
  r.SkipSpace();
  bool any = false;
  if (r.IdentStart()) {
    // "none" alone.
    Reader probe(text);
    probe.SkipSpace();
    if (probe.Name() == "none") {
      probe.SkipSpace();
      if (probe.End()) { any = true; goto finish; }
    }
  }
  while (true) {
    r.SkipSpace();
    if (r.End()) break;
    if (!r.IdentStart()) return false;
    const std::string name = r.Name();
    if (!r.Consume('(')) return false;
    // Reads the next value and the separator rules of each function.
    std::vector<Token> args;
    auto next = [&](bool first) -> bool {
      r.SkipSpace();
      if (!first) {
        if (!r.Consume(',')) return false;
        r.SkipSpace();
      }
      Token t;
      if (!r.Numeric(&t)) return false;
      args.push_back(t);
      return true;
    };
    auto optionalComma = [&]() -> bool {
      r.SkipSpace();
      if (!r.Consume(',')) return false;
      return true;
    };
    T3 op;
    bool is3D = false;
    if (name == "matrix" || name == "matrix3d") {
      const int n = name == "matrix" ? 6 : 16;
      for (int k = 0; k < n; ++k) if (!next(k == 0)) return false;
      float v[16];
      for (int k = 0; k < n; ++k) if (!PlainNumber(args[k], &v[k])) return false;
      if (n == 6) {
        op.m[0][0] = v[0]; op.m[0][1] = v[1]; op.m[1][0] = v[2]; op.m[1][1] = v[3];
        op.m[3][0] = v[4]; op.m[3][1] = v[5];
      } else {
        for (int k = 0; k < 16; ++k) op.m[k / 4][k % 4] = v[k];
        is3D = true;
      }
    } else if (name == "translate" || name == "translatex" || name == "translatey" ||
               name == "translatez" || name == "translate3d") {
      float px[3] = {0, 0, 0};
      if (name == "translate3d") {
        for (int k = 0; k < 3; ++k) if (!next(k == 0)) return false;
        for (int k = 0; k < 3; ++k) if (!AbsolutePx(args[k], &px[k])) return false;
        is3D = true;
      } else {
        if (!next(true)) return false;
        const int axis = name == "translatey" ? 1 : name == "translatez" ? 2 : 0;
        if (!AbsolutePx(args[0], &px[axis])) return false;
        if (name == "translatez") is3D = true;
        if (name == "translate" && optionalComma()) {
          r.SkipSpace();
          Token t;
          if (!r.Numeric(&t) || !AbsolutePx(t, &px[1])) return false;
        }
      }
      op = Translation(double(px[0]), double(px[1]), double(px[2]));
    } else if (name == "scale" || name == "scalex" || name == "scaley" || name == "scalez" ||
               name == "scale3d") {
      float s[3] = {1, 1, 1};
      if (name == "scale3d") {
        for (int k = 0; k < 3; ++k) if (!next(k == 0)) return false;
        for (int k = 0; k < 3; ++k) if (!NumberOrPercentage(args[k], &s[k])) return false;
        is3D = true;
      } else {
        if (!next(true)) return false;
        float v;
        if (!NumberOrPercentage(args[0], &v)) return false;
        if (name == "scale") {
          s[0] = s[1] = v;
          if (optionalComma()) {
            r.SkipSpace();
            Token t;
            if (!r.Numeric(&t) || !NumberOrPercentage(t, &s[1])) return false;
          }
        } else {
          const int axis = name == "scaley" ? 1 : name == "scalez" ? 2 : 0;
          s[axis] = v;
          if (name == "scalez") is3D = true;
        }
      }
      op = Scale(double(s[0]), double(s[1]), double(s[2]));
    } else if (name == "rotate" || name == "rotatex" || name == "rotatey" || name == "rotatez") {
      if (!next(true)) return false;
      double theta;
      if (!Radians(args[0], &theta)) return false;
      if (name == "rotatex") op = Rotation(1., 0., 0., theta);
      else if (name == "rotatey") op = Rotation(0., 1., 0., theta);
      else op = Rotation(0., 0., 1., theta);
      is3D = name != "rotate";
    } else if (name == "rotate3d") {
      for (int k = 0; k < 4; ++k) if (!next(k == 0)) return false;
      float a[3];
      for (int k = 0; k < 3; ++k) if (!PlainNumber(args[k], &a[k])) return false;
      double theta;
      if (!Radians(args[3], &theta)) return false;
      // get_normalized_vector_and_angle (f32).
      const float square = a[0] * a[0] + a[1] * a[1] + a[2] * a[2];
      float x, y, z;
      if (std::fabs(square - 0.0f) < 1.0e-6f) {
        x = 0; y = 0; z = 1; theta = 0;
      } else {
        float length = std::sqrt(square);
        float vx = a[0], vy = a[1], vz = a[2];
        if (std::isinf(length)) {
          const float max = std::numeric_limits<float>::max();
          vx /= max; vy /= max; vz /= max;
          length = std::sqrt(vx * vx + vy * vy + vz * vz);
        }
        x = vx / length; y = vy / length; z = vz / length;
      }
      op = Rotation(double(x), double(y), double(z), theta);
      is3D = true;
    } else if (name == "skew" || name == "skewx" || name == "skewy") {
      if (!next(true)) return false;
      double ax = 0, ay = 0;
      double first;
      if (!Radians(args[0], &first)) return false;
      if (name == "skewy") ay = first; else ax = first;
      if (name == "skew" && optionalComma()) {
        r.SkipSpace();
        Token t;
        if (!r.Numeric(&t) || !Radians(t, &ay)) return false;
      }
      op = Skew(ax, ay);
    } else if (name == "perspective") {
      r.SkipSpace();
      Token t;
      float d = std::numeric_limits<float>::infinity();
      if (r.Numeric(&t)) {
        if (!AbsolutePx(t, &d) || d < 0) return false;
      } else if (r.Name() != "none") {
        return false;
      }
      if (std::isfinite(d)) {
        d = std::max(d, 1.0f);
        op.m[2][3] = double(-1.0f / d);
      }
      is3D = true;
    } else {
      return false;
    }
    r.SkipSpace();
    if (!r.Consume(')')) return false;
    contains3D = contains3D || is3D;
    acc = op.Then(acc);
    any = true;
  }
finish:
  if (!any) return false;
  for (int k = 0; k < 16; ++k) {
    const double v = acc.m[k / 4][k % 4];
    // Rust f64::min/max ignore a NaN operand.
    double c = std::isnan(v) ? double(std::numeric_limits<float>::max())
                             : std::max(std::min(v, double(std::numeric_limits<float>::max())),
                                        double(std::numeric_limits<float>::lowest()));
    out[k] = float(c);
  }
  *is3D = contains3D;
  return true;
}
}  // namespace ruyijs::domain::geometry
