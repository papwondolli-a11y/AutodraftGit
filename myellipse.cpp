#include "myellipse.h"
#include "myarc.h"
#include "mycircle.h"
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

// ── 곡선 교점용 "부호 있는 거리" 함수 (상대 곡선 바깥 +, 안 −, 위 0) ──
//   타원 매개각 t 를 따라 이 값의 부호가 바뀌는 곳·0 에 닿는 곳이 교점·접점
struct CurveDistance {
  virtual ~CurveDistance() {}
  virtual double at(const MyVector &p) const = 0;
};

// 원 (호도 같은 원으로 계산하고 각도 범위는 나중에 거름)
struct CircleDistance : public CurveDistance {
  MyVector c;
  double r;
  CircleDistance(const MyVector &center, double radius) : c(center), r(radius) {}
  double at(const MyVector &p) const override { return p.distanceTo(c) - r; }
};

// 타원: 타원 좌표 반지름 ρ = |(x', y')| 로 (ρ − 1) × 단축 반지름 ≈ 거리 (곡선 근처에서 단조)
struct EllipseDistance : public CurveDistance {
  const MyEllipse *e;
  explicit EllipseDistance(const MyEllipse *ell) : e(ell) {}
  double at(const MyVector &p) const override {
    const MyVector u = e->majorAxis, v = e->minorAxis();
    const double uu = u.x * u.x + u.y * u.y, vv = v.x * v.x + v.y * v.y;
    if (uu < 1.0e-24 || vv < 1.0e-24)
      return 1.0e300;
    const double dx = p.x - e->center.x, dy = p.y - e->center.y;
    const double x = (dx * u.x + dy * u.y) / uu, y = (dx * v.x + dy * v.y) / vv;
    return (std::sqrt(x * x + y * y) - 1.0) * e->minorRadius();
  }
};

// 각도 deg 가 호(start, span 도) 범위 안인가 — 끝에서 tolDeg 까지 허용 (호 끝이 상대 곡선 위에 닿은 경우)
bool arcContains(const MyArc *a, double deg, double tolDeg) {
  double start = a->startAngle, span = a->spanAngle;
  if (span < 0.0) {
    start += span;
    span = -span;
  }
  if (span >= 360.0 - 1.0e-9)
    return true;
  double rel = std::fmod(deg - start, 360.0);
  if (rel < 0.0)
    rel += 360.0;
  return rel <= span + tolDeg || rel >= 360.0 - tolDeg;
}

// 타원 e 의 매개각 t ∈ [0, 2π) 중 fn(P(t)) = 0 인 곳들 (교차 + 접점). 호 범위는 부르는 쪽에서 거름
//   ① 720 등분 표본에서 부호가 바뀌는 칸 → 이분법 60 회
//   ② 부호는 안 바뀌지만 |fn| 이 국소 최소인 칸 → 황금분할로 최소점, |fn| ≤ tol 이면 접점
std::vector<double> curveRoots(const MyEllipse *e, const CurveDistance &fn, double tol) {
  std::vector<double> roots;
  const int n = 720;
  const double h = kTwoPi / n;
  std::vector<double> f(n + 1);
  for (int i = 0; i <= n; ++i)
    f[i] = fn.at(e->pointAt(h * i));
  for (int i = 0; i < n; ++i) {
    const double a0 = h * i, a1 = h * (i + 1);
    if (f[i] == 0.0) {
      roots.push_back(a0);
      continue;
    }
    if ((f[i] < 0.0) != (f[i + 1] < 0.0) && f[i + 1] != 0.0) {
      double lo = a0, hi = a1, flo = f[i];
      for (int it = 0; it < 60; ++it) {
        const double mid = 0.5 * (lo + hi);
        const double fm = fn.at(e->pointAt(mid));
        if ((fm < 0.0) == (flo < 0.0)) {
          lo = mid;
          flo = fm;
        } else {
          hi = mid;
        }
      }
      roots.push_back(0.5 * (lo + hi));
    }
  }
  // 접점: 표본 i 에서 |f| 국소 최소이고 양옆 칸에 부호 바뀜이 없을 때
  for (int i = 0; i < n; ++i) {
    const int ip = (i + n - 1) % n, in = i + 1;
    const double fa = std::fabs(f[ip]), fb = std::fabs(f[i]), fc = std::fabs(f[in]);
    if (!(fb <= fa && fb <= fc))
      continue;
    if ((f[ip] < 0.0) != (f[i] < 0.0) || (f[i] < 0.0) != (f[in] < 0.0))
      continue;                                         // 교차는 위에서 찾음
    double lo = h * (i - 1), hi = h * (i + 1);
    for (int it = 0; it < 80; ++it) {
      const double m1 = lo + (hi - lo) * 0.381966, m2 = hi - (hi - lo) * 0.381966;
      if (std::fabs(fn.at(e->pointAt(m1))) < std::fabs(fn.at(e->pointAt(m2))))
        hi = m2;
      else
        lo = m1;
    }
    const double tm = 0.5 * (lo + hi);
    if (std::fabs(fn.at(e->pointAt(tm))) <= tol)
      roots.push_back(wrap2Pi(tm));
  }
  // 같은 근 정리 (칸 경계에서 두 번 잡힌 것)
  std::sort(roots.begin(), roots.end());
  std::vector<double> out;
  for (double t : roots)
    if (out.empty() || std::fabs(t - out.back()) > 1.0e-9)
      out.push_back(t);
  if (out.size() > 1 && std::fabs(out.front() + kTwoPi - out.back()) <= 1.0e-9)
    out.pop_back();                                     // 0 과 2π 는 같은 점
  return out;
}

// 타원(호) 매개각 범위 안인가 — 끝에서 tolT(라디안) 까지 허용
bool ellipseContains(const MyEllipse *e, double t, double tolT) {
  if (e->isFull())
    return true;
  const double rel = wrap2Pi(t - e->startParam);
  return rel <= e->span() + tolT || rel >= kTwoPi - tolT;
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

  // 선분: 정확한 해석해 (근사 현은 실제 타원보다 안쪽이라 타원 위에서 끝나는 선의 교점을 놓침)
  if (other->type() == EntityType::Line)
    return intersectSegment(static_cast<const MyLine *>(other)->start,
                            static_cast<const MyLine *>(other)->end);

  // 원·호·타원: 이 타원 위에서 상대 곡선까지의 부호 있는 거리가 0 인 매개각을 정밀하게 찾음
  //   (예전 360분할 현 근사는 곡선 위에서 끝나는 호·타원호의 교점을 놓쳤음)
  const double lenTol = std::max(1.0e-3, majorRadius() * 1.0e-6);
  switch (other->type()) {
  case EntityType::Circle: {
    const MyCircle *c = static_cast<const MyCircle *>(other);
    const CircleDistance fn(c->center, c->radius);
    const std::vector<double> ts = curveRoots(this, fn, lenTol);
    for (double t : ts)
      if (ellipseContains(this, t, lenTol / std::max(1.0e-12, minorRadius())))
        result.push_back(pointAt(t));
    break;
  }
  case EntityType::Arc: {
    const MyArc *a = static_cast<const MyArc *>(other);
    const CircleDistance fn(a->center, a->radius);
    const std::vector<double> ts = curveRoots(this, fn, lenTol);
    const double tolDeg = MyMath::rad2deg(lenTol / std::max(1.0e-12, a->radius));
    for (double t : ts) {
      const MyVector p = pointAt(t);
      const double deg = MyMath::rad2deg(std::atan2(p.y - a->center.y, p.x - a->center.x));
      if (ellipseContains(this, t, lenTol / std::max(1.0e-12, minorRadius())) && arcContains(a, deg, tolDeg))
        result.push_back(p);
    }
    break;
  }
  case EntityType::Ellipse: {
    const MyEllipse *e = static_cast<const MyEllipse *>(other);
    const EllipseDistance fn(e);
    const std::vector<double> ts = curveRoots(this, fn, lenTol);
    const double otherTol = std::max(1.0e-3, e->majorRadius() * 1.0e-6) / std::max(1.0e-12, e->minorRadius());
    for (double t : ts) {
      const MyVector p = pointAt(t);
      if (ellipseContains(this, t, lenTol / std::max(1.0e-12, minorRadius())) &&
          ellipseContains(e, e->paramAt(p), otherTol))
        result.push_back(p);
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

std::vector<MyVector> MyEllipse::intersectSegment(const MyVector &p0, const MyVector &p1) const {
  std::vector<MyVector> out;
  const MyVector u = majorAxis, v = minorAxis();
  const double uu = u.x * u.x + u.y * u.y, vv = v.x * v.x + v.y * v.y;
  if (uu < 1.0e-24 || vv < 1.0e-24)
    return out;
  // 타원 좌표 (x', y') = (d·u/|u|², d·v/|v|²) 로 바꾸면 타원 = 단위원 (u ⟂ v)
  const double dx0 = p0.x - center.x, dy0 = p0.y - center.y;
  const double ex = p1.x - p0.x, ey = p1.y - p0.y;
  const double qx = (dx0 * u.x + dy0 * u.y) / uu, qy = (dx0 * v.x + dy0 * v.y) / vv;
  const double rx = (ex * u.x + ey * u.y) / uu, ry = (ex * v.x + ey * v.y) / vv;
  // |q + s r|² = 1
  const double A = rx * rx + ry * ry;
  const double B = 2.0 * (qx * rx + qy * ry);
  const double C = qx * qx + qy * qy - 1.0;
  if (A < 1.0e-30)
    return out;
  double disc = B * B - 4.0 * A * C;
  const double len = std::sqrt(ex * ex + ey * ey);
  // 허용오차: 선분 끝이 타원 위에 "닿은" 경우 — 스냅 점이 화면용 곡선(베지에)에서 왔으면 미세하게 어긋날 수 있음
  //   0.001 mm 또는 장축의 1e-6 중 큰 값만큼 선분을 늘려 봄
  const double lenTol = std::max(1.0e-3, majorRadius() * 1.0e-6);
  const double sTol = len > 1.0e-12 ? lenTol / len : 0.0;
  if (disc < 0.0) {
    if (disc > -1.0e-9 * B * B - 1.0e-12)
      disc = 0.0;                                       // 접선 (수치 오차)
    else
      return out;
  }
  const double sq = std::sqrt(disc);
  const double roots[2] = {(-B - sq) / (2.0 * A), (-B + sq) / (2.0 * A)};
  const int nRoots = disc == 0.0 ? 1 : 2;
  for (int i = 0; i < nRoots; ++i) {
    double s = roots[i];
    if (s < -sTol || s > 1.0 + sTol)
      continue;                                         // 선분 밖
    s = std::max(0.0, std::min(1.0, s));
    const double t = std::atan2(qy + s * ry, qx + s * rx);
    if (!containsParam(t))
      continue;                                         // 타원호 밖
    out.push_back(MyVector(p0.x + ex * s, p0.y + ey * s));
  }
  return out;
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
