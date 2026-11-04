#include "myellipse.h"
#include "myline.h"
#include "mymath.h"
#include "mypolyline.h"
#include "myspline.h"
#include <algorithm>
#include <cmath>

namespace {
const double kTwoPi = 6.283185307179586;

double wrap2Pi(double t) {
  t = std::fmod(t, kTwoPi);
  if (t < 0.0)
    t += kTwoPi;
  return t;
}
} // namespace

MyEllipse::MyEllipse()
    : center(0, 0), majorAxis(1, 0), ratio(1.0), startParam(0.0),
      endParam(kTwoPi) {}

MyEllipse::MyEllipse(const MyVector &c, const MyVector &major, double r,
                     double startP, double endP)
    : center(c), majorAxis(major), ratio(r), startParam(startP),
      endParam(endP) {
  normalizeParams();
}

MyVector MyEllipse::minorAxis() const {
  return MyVector(-majorAxis.y * ratio, majorAxis.x * ratio);
}

double MyEllipse::rotationRad() const {
  return std::atan2(majorAxis.y, majorAxis.x);
}

bool MyEllipse::isFull() const {
  return std::fabs(endParam - startParam - kTwoPi) < 1.0e-9;
}

MyVector MyEllipse::pointAt(double t) const {
  MyVector mn = minorAxis();
  double c = std::cos(t), s = std::sin(t);
  return MyVector(center.x + majorAxis.x * c + mn.x * s,
                  center.y + majorAxis.y * c + mn.y * s);
}

double MyEllipse::paramAt(const MyVector &p) const {
  double a = majorRadius();
  double b = minorRadius();
  if (a < 1.0e-12 || b < 1.0e-12)
    return 0.0;
  double rot = rotationRad();
  double dx = p.x - center.x, dy = p.y - center.y;
  double lx = dx * std::cos(rot) + dy * std::sin(rot);
  double ly = -dx * std::sin(rot) + dy * std::cos(rot);
  return std::atan2(ly / b, lx / a);
}

bool MyEllipse::containsParam(double t) const {
  if (isFull())
    return true;
  double rel = wrap2Pi(t - startParam);
  return rel <= (endParam - startParam) + 1.0e-9;
}

void MyEllipse::normalizeParams() {
  double sp = endParam - startParam;
  if (sp <= 1.0e-12 || sp > kTwoPi - 1.0e-9) {
    startParam = 0.0;
    endParam = kTwoPi;
    return;
  }
  startParam = wrap2Pi(startParam);
  endParam = startParam + sp;
}

std::vector<MyVector> MyEllipse::samplePoints(int segments) const {
  std::vector<MyVector> pts;
  if (segments < 4)
    segments = 4;
  double sp = endParam - startParam;
  int n = std::max(4, (int)std::ceil(segments * sp / kTwoPi));
  pts.reserve(n + 1);
  for (int i = 0; i <= n; ++i)
    pts.push_back(pointAt(startParam + sp * double(i) / double(n)));
  return pts;
}

std::vector<MyVector> MyEllipse::intersect(const MyEntity *other) const {
  std::vector<MyVector> result;
  if (!other)
    return result;

  std::vector<MyVector> pts = samplePoints(360);
  size_t segCount = pts.size() >= 2 ? pts.size() - 1 : 0;

  switch (other->type()) {
  case EntityType::Line:
  case EntityType::Circle:
  case EntityType::Arc: {
    for (size_t i = 0; i < segCount; ++i) {
      MyLine seg(pts[i], pts[i + 1]);
      std::vector<MyVector> r = seg.intersect(other);
      result.insert(result.end(), r.begin(), r.end());
    }
    break;
  }
  case EntityType::Ellipse: {
    const MyEllipse *e = static_cast<const MyEllipse *>(other);
    std::vector<MyVector> q = e->samplePoints(360);
    for (size_t i = 0; i < segCount; ++i) {
      MyLine s1(pts[i], pts[i + 1]);
      for (size_t j = 0; j + 1 < q.size(); ++j) {
        MyLine s2(q[j], q[j + 1]);
        std::vector<MyVector> r = MyMath::intersectLineLine(s1, s2);
        result.insert(result.end(), r.begin(), r.end());
      }
    }
    break;
  }
  case EntityType::Polyline:
    return other->intersect(this);
  case EntityType::Spline: {
    MyPolyline poly(
        MyMath::approximateSpline(*static_cast<const MySpline *>(other), 20),
        false);
    return poly.intersect(this);
  }
  default:
    return result;
  }

  // 샘플 선분 경계에서 중복 검출된 교점 제거
  std::vector<MyVector> unique;
  double tol = std::max(1.0e-6, majorRadius() * 1.0e-6);
  for (const MyVector &p : result) {
    bool dup = false;
    for (const MyVector &u : unique) {
      if (p.distanceTo(u) < tol) {
        dup = true;
        break;
      }
    }
    if (!dup)
      unique.push_back(p);
  }
  return unique;
}

MyVector MyEllipse::getNearestPoint(const MyVector &pos) const {
  const int N = 360;
  double sp = endParam - startParam;
  double bestT = startParam;
  double bestD = 1.0e300;
  for (int i = 0; i <= N; ++i) {
    double t = startParam + sp * double(i) / double(N);
    double d = pointAt(t).distanceTo(pos);
    if (d < bestD) {
      bestD = d;
      bestT = t;
    }
  }
  // 최근접 샘플 주변을 이분 탐색(황금분할)으로 정밀화
  double step = sp / double(N);
  double lo = std::max(startParam, bestT - step);
  double hi = std::min(endParam, bestT + step);
  for (int it = 0; it < 40; ++it) {
    double m1 = lo + (hi - lo) / 3.0;
    double m2 = hi - (hi - lo) / 3.0;
    if (pointAt(m1).distanceTo(pos) < pointAt(m2).distanceTo(pos))
      hi = m2;
    else
      lo = m1;
  }
  return pointAt((lo + hi) * 0.5);
}
