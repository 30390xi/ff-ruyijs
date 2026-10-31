#ifndef _WIN32
#include "web_geometry/svg_path.h"
#include <CoreGraphics/CoreGraphics.h>
#include <algorithm>
#include <cmath>

namespace ruyijs::web_geometry {
namespace {
struct PathHold {
  CGMutablePathRef path = CGPathCreateMutable();
  ~PathHold() { if (path) CGPathRelease(path); }
  PathHold(const PathHold&) = delete;
  PathHold& operator=(const PathHold&) = delete;
  PathHold() = default;
};

void PointAt(double theta, double rx, double ry, double cphi, double sphi, double ocx, double ocy,
             double* ox, double* oy) {
  const double px = rx * std::cos(theta), py = ry * std::sin(theta);
  *ox = cphi * px - sphi * py + ocx;
  *oy = sphi * px + cphi * py + ocy;
}

void TangentAt(double theta, double rx, double ry, double cphi, double sphi, double* tx, double* ty) {
  const double px = -rx * std::sin(theta), py = ry * std::cos(theta);
  *tx = cphi * px - sphi * py;
  *ty = sphi * px + cphi * py;
}

bool AddArc(CGMutablePathRef path, double x1, double y1, const std::array<float, 7>& v) {
  double rx = std::fabs(v[0]), ry = std::fabs(v[1]);
  const double x2 = v[5], y2 = v[6];
  if (!(rx > 0) || !(ry > 0) || std::hypot(x2 - x1, y2 - y1) < 1e-9) {
    CGPathAddLineToPoint(path, nullptr, x2, y2);
    return true;
  }
  const double phi = double(v[2]) * (std::acos(-1.0) / 180.0);
  const bool large = v[3] != 0, sweep = v[4] != 0;
  const double cphi = std::cos(phi), sphi = std::sin(phi);
  const double dx = (x1 - x2) / 2, dy = (y1 - y2) / 2;
  const double x1p = cphi * dx + sphi * dy;
  const double y1p = -sphi * dx + cphi * dy;
  const double lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
  if (lam > 1) {
    const double scale = std::sqrt(lam);
    rx *= scale;
    ry *= scale;
  }
  const double num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
  const double den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
  if (!(den > 0) || !std::isfinite(num)) {
    CGPathAddLineToPoint(path, nullptr, x2, y2);
    return true;
  }
  const double root = std::sqrt(std::max(0.0, num / den));
  const double sign = large == sweep ? -1.0 : 1.0;
  const double cxp = sign * root * rx * y1p / ry;
  const double cyp = sign * root * -ry * x1p / rx;
  const double ocx = cphi * cxp - sphi * cyp + (x1 + x2) / 2;
  const double ocy = sphi * cxp + cphi * cyp + (y1 + y2) / 2;
  auto between = [](double ux, double uy, double vx, double vy) {
    const double un = std::hypot(ux, uy), vn = std::hypot(vx, vy);
    if (!(un > 0) || !(vn > 0)) return 0.0;
    double cosine = (ux * vx + uy * vy) / (un * vn);
    if (cosine > 1) cosine = 1;
    else if (cosine < -1) cosine = -1;
    double angle = std::acos(cosine);
    if (ux * vy - uy * vx < 0) angle = -angle;
    return angle;
  };
  const double ux = (x1p - cxp) / rx, uy = (y1p - cyp) / ry;
  const double vx = (-x1p - cxp) / rx, vy = (-y1p - cyp) / ry;
  const double theta = between(1, 0, ux, uy);
  double delta = between(ux, uy, vx, vy);
  const double turn = std::acos(-1.0) * 2;
  if (!sweep && delta > 0) delta -= turn;
  else if (sweep && delta < 0) delta += turn;
  const int segments = std::max(1, int(std::ceil(std::fabs(delta) / (std::acos(-1.0) / 2) - 1e-12)));
  const double step = delta / segments;
  // Cubic controls are the affine image of the circular 4/3 tan(dt/4) fit.
  const double handle = (4.0 / 3.0) * std::tan(step / 4.0);
  for (int i = 0; i < segments; ++i) {
    const double t1 = theta + step * i;
    const double t2 = theta + step * (i + 1);
    double sx = x1, sy = y1, ex, ey, d1x, d1y, d2x, d2y;
    if (i != 0) PointAt(t1, rx, ry, cphi, sphi, ocx, ocy, &sx, &sy);
    if (i + 1 == segments) { ex = x2; ey = y2; }
    else PointAt(t2, rx, ry, cphi, sphi, ocx, ocy, &ex, &ey);
    TangentAt(t1, rx, ry, cphi, sphi, &d1x, &d1y);
    TangentAt(t2, rx, ry, cphi, sphi, &d2x, &d2y);
    if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(ex) || !std::isfinite(ey)) return false;
    CGPathAddCurveToPoint(path, nullptr, sx + handle * d1x, sy + handle * d1y,
                          ex - handle * d2x, ey - handle * d2y, ex, ey);
  }
  return true;
}
}

bool SvgPathBounds(const std::vector<ruyijs::domain::geometry::SvgPathCommand>& commands,
                   float stroke, ruyijs::domain::geometry::SvgBox* out) {
  PathHold hold;
  if (!hold.path) return false;
  bool open = false;
  double x = 0, y = 0, startX = 0, startY = 0;
  for (const auto& command : commands) {
    const auto& v = command.values;
    switch (command.kind) {
      case 'M':
        CGPathMoveToPoint(hold.path, nullptr, v[0], v[1]);
        x = startX = v[0];
        y = startY = v[1];
        open = true;
        break;
      case 'Z':
        if (open) CGPathCloseSubpath(hold.path);
        x = startX;
        y = startY;
        open = false;
        break;
      case 'L':
        CGPathAddLineToPoint(hold.path, nullptr, v[0], v[1]);
        x = v[0];
        y = v[1];
        break;
      case 'C':
        CGPathAddCurveToPoint(hold.path, nullptr, v[0], v[1], v[2], v[3], v[4], v[5]);
        x = v[4];
        y = v[5];
        break;
      case 'Q':
        CGPathAddQuadCurveToPoint(hold.path, nullptr, v[0], v[1], v[2], v[3]);
        x = v[2];
        y = v[3];
        break;
      case 'A':
        if (!AddArc(hold.path, x, y, v)) return false;
        x = v[5];
        y = v[6];
        break;
      default:
        return false;
    }
  }
  CGRect bounds;
  if (stroke > 0) {
    CGPathRef widened = CGPathCreateCopyByStrokingPath(
        hold.path, nullptr, stroke, kCGLineCapButt, kCGLineJoinMiter, 4);
    if (!widened) return false;
    bounds = CGPathGetPathBoundingBox(widened);
    CGPathRelease(widened);
  } else {
    bounds = CGPathGetPathBoundingBox(hold.path);
  }
  if (CGRectIsNull(bounds) || CGRectIsInfinite(bounds) || bounds.size.width < 0 || bounds.size.height < 0) {
    *out = {};
    return true;
  }
  if (!std::isfinite(bounds.origin.x) || !std::isfinite(bounds.origin.y) ||
      !std::isfinite(bounds.size.width) || !std::isfinite(bounds.size.height)) return false;
  *out = {float(bounds.origin.x), float(bounds.origin.y), float(bounds.size.width), float(bounds.size.height)};
  return true;
}
}
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <wrl/client.h>
#include "web_geometry/svg_path.h"
#include <cmath>
#pragma comment(lib, "d2d1.lib")

namespace ruyijs::web_geometry {
bool SvgPathBounds(const std::vector<ruyijs::domain::geometry::SvgPathCommand>& commands,
                   float stroke, ruyijs::domain::geometry::SvgBox* out) {
  using Microsoft::WRL::ComPtr;
  ComPtr<ID2D1Factory> factory;
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()))) return false;
  ComPtr<ID2D1PathGeometry> path;
  ComPtr<ID2D1GeometrySink> sink;
  if (FAILED(factory->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) return false;
  bool open = false;
  for (const auto& command : commands) {
    const auto& v = command.values;
    switch (command.kind) {
      case 'M':
        if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->BeginFigure({v[0],v[1]}, D2D1_FIGURE_BEGIN_FILLED); open=true; break;
      case 'Z':
        if (open) sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        open=false; break;
      case 'L': sink->AddLine({v[0],v[1]}); break;
      case 'C': sink->AddBezier({{v[0],v[1]},{v[2],v[3]},{v[4],v[5]}}); break;
      case 'Q': sink->AddQuadraticBezier({{v[0],v[1]},{v[2],v[3]}}); break;
      case 'A': sink->AddArc({{v[5],v[6]},{std::abs(v[0]),std::abs(v[1])},v[2],
          v[4] ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
          v[3] ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL}); break;
      default: return false;
    }
  }
  if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
  if (FAILED(sink->Close())) return false;
  D2D1_RECT_F bounds{};
  HRESULT status;
  if (stroke > 0) {
    ComPtr<ID2D1StrokeStyle> style;
    auto properties = D2D1::StrokeStyleProperties(); properties.miterLimit=4;
    properties.lineJoin=D2D1_LINE_JOIN_MITER_OR_BEVEL;
    if (FAILED(factory->CreateStrokeStyle(properties, nullptr, 0, &style))) return false;
    status=path->GetWidenedBounds(stroke, style.Get(), nullptr, .01f, &bounds);
  } else status=path->GetBounds(nullptr, &bounds);
  if (FAILED(status)) return false;
  *out = bounds.right < bounds.left || bounds.bottom < bounds.top ? ruyijs::domain::geometry::SvgBox{}
      : ruyijs::domain::geometry::SvgBox{bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top};
  return true;
}
}

#endif
