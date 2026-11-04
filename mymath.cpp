#include "mymath.h"
#include "myarc.h"
#include "mycircle.h"
#include "myline.h"
#include "myvector.h"
#include "myspline.h"
#include "mypolyline.h"

#include <algorithm>
#include <cmath>

// 상수 정의
const double MyMath::TOLERANCE = 1.0e-9;
const double MyMath::PI = 3.14159265358979323846;

// =========================================================
// 1. 기본 유틸리티 구현
// =========================================================

bool MyMath::isEqual(double a, double b, double tol) {
  return std::abs(a - b) < tol;
}

bool MyMath::isZero(double a, double tol) { return std::abs(a) < tol; }

double MyMath::deg2rad(double deg) { return deg * (PI / 180.0); }

double MyMath::rad2deg(double rad) { return rad * (180.0 / PI); }

double MyMath::normalizeAngle(double angle) {
  while (angle < 0.0)
    angle += 360.0;
  while (angle >= 360.0)
    angle -= 360.0;
  return angle;
}

bool MyMath::isAngleBetween(double angle, double start, double span) {
    // 1. 360도 이상인 경우 무조건 범위 내에 포함됨
    if (std::abs(span) >= 360.0 - 1e-5) {
        return true;
    }

    angle = normalizeAngle(angle);
    start = normalizeAngle(start);

    // 2. 시계(CW) 방향 각도도 반시계(CCW) 기준으로 정규화하여 물리적 시작점 계산
    double startCcw = start;
    if (span < 0.0) {
        startCcw = normalizeAngle(start + span);
    }

    // 3. 주어진 각도와 물리적 시작점 간의 반시계방향 거리 (차이)
    double dist = normalizeAngle(angle - startCcw);
    double absSpan = std::abs(span);

    // [핵심 수정 2] 부동소수점 오차 허용 (교차점이 호의 끝점에 정확히 걸칠 때의 오차 방지)
    double angleTol = 1e-3;
    if (dist <= absSpan + angleTol || dist >= 360.0 - angleTol) {
        return true;
    }

    return false;
}

double MyMath::dotProduct(const MyVector &v1, const MyVector &v2) {
  return v1.x * v2.x + v1.y * v2.y; // 2D 평면 기준
}

double MyMath::crossProduct2D(const MyVector &v1, const MyVector &v2) {
  return v1.x * v2.y - v1.y * v2.x;
}

// =========================================================
// 2. 단순 스냅 구현
// =========================================================

MyVector MyMath::snapToGrid(const MyVector &pos, double gridSize) {
  if (gridSize < TOLERANCE)
    return pos;
  double x = std::round(pos.x / gridSize) * gridSize;
  double y = std::round(pos.y / gridSize) * gridSize;
  return MyVector(x, y, pos.z);
}

MyVector MyMath::getMidpoint(const MyVector &p1, const MyVector &p2) {
  return (p1 + p2) * 0.5;
}

std::vector<MyVector> MyMath::getQuadrants(const MyCircle &circle) {
  std::vector<MyVector> quads;
  // 0도, 90도, 180도, 270도 지점
  quads.push_back(circle.center + MyVector(circle.radius, 0));
  quads.push_back(circle.center + MyVector(0, circle.radius));
  quads.push_back(circle.center + MyVector(-circle.radius, 0));
  quads.push_back(circle.center + MyVector(0, -circle.radius));
  return quads;
}

// =========================================================
// 3. 근처 점(Nearest Snap) 구현
// =========================================================

MyVector MyMath::getNearestPointOnSegment(const MyVector &pos,
                                          const MyLine &line) {
  MyVector ab = line.end - line.start;
  MyVector ap = pos - line.start;

  double lenSq = dotProduct(ab, ab);
  if (lenSq < TOLERANCE)
    return line.start; // 선분 길이가 0인 경우

  // 투영(Projection) 공식: t = (ap . ab) / |ab|^2
  double t = dotProduct(ap, ab) / lenSq;

  // 선분 범위 [0, 1]로 클램핑(Clamping)
  if (t < 0.0)
    t = 0.0;
  else if (t > 1.0)
    t = 1.0;

  return line.start + ab * t;
}

MyVector MyMath::getNearestPointOnCircle(const MyVector &pos,
                                         const MyCircle &circle) {
  MyVector dir = pos - circle.center;
  double dist = dir.magnitude();

  if (dist < TOLERANCE) {
    // 마우스가 정확히 중심에 있을 때 예외 처리 (0도 방향 반환)
    return circle.center + MyVector(circle.radius, 0);
  }

  // 단위 벡터로 만든 후 반지름만큼 곱함
  return circle.center + dir * (circle.radius / dist);
}

MyVector MyMath::getNearestPointOnArc(const MyVector &pos, const MyArc &arc) {
  // 1. 일단 원 전체에서의 근처 점을 구함
  MyCircle tempCircle(arc.center, arc.radius);
  MyVector onCircle = getNearestPointOnCircle(pos, tempCircle);

  // 2. 해당 점의 각도가 호 범위 내에 있는지 확인
  double angle = normalizeAngle(
      rad2deg(std::atan2(onCircle.y - arc.center.y, onCircle.x - arc.center.x)));

  if (isAngleBetween(angle, arc.startAngle, arc.spanAngle)) {
    return onCircle;
  }

  // 3. 범위 밖이라면 시작점이나 끝점 중 가까운 곳 반환
  MyVector startPt = arc.startPoint();
  MyVector endPt = arc.endPoint();

  if (pos.distanceTo(startPt) < pos.distanceTo(endPt)) {
    return startPt;
  }
  return endPt;
}

MyVector MyMath::getPerpendicularPointOnLine(const MyVector &pos, const MyLine &line) {
  MyVector ab = line.end - line.start;
  MyVector ap = pos - line.start;
  double lenSq = dotProduct(ab, ab);
  if (lenSq < TOLERANCE) return line.start;
  double t = dotProduct(ap, ab) / lenSq;
  return line.start + ab * t;
}

MyVector MyMath::getPerpendicularPointOnCircle(const MyVector &pos, const MyCircle &circle) {
  return getNearestPointOnCircle(pos, circle);
}

MyVector MyMath::getPerpendicularPointOnArc(const MyVector &pos, const MyArc &arc) {
  return getNearestPointOnArc(pos, arc);
}

// =========================================================
// 4. 교차점(Intersection) 구현
// =========================================================

std::vector<MyVector> MyMath::intersectLineLine(const MyLine &l1,
                                                const MyLine &l2) {
  std::vector<MyVector> results;

  double x1 = l1.start.x, y1 = l1.start.y;
  double x2 = l1.end.x, y2 = l1.end.y;
  double x3 = l2.start.x, y3 = l2.start.y;
  double x4 = l2.end.x, y4 = l2.end.y;

  double denominator = (y4 - y3) * (x2 - x1) - (x4 - x3) * (y2 - y1);

  if (std::abs(denominator) < TOLERANCE) {
    return results; // 평행하거나 일치
  }

  double ua = ((x4 - x3) * (y1 - y3) - (y4 - y3) * (x1 - x3)) / denominator;
  double ub = ((x2 - x1) * (y1 - y3) - (y2 - y1) * (x1 - x3)) / denominator;

  // 두 선분 범위 [0, 1] 내에 교점이 있는지 확인
  if (ua >= 0.0 && ua <= 1.0 && ub >= 0.0 && ub <= 1.0) {
    results.push_back(MyVector(x1 + ua * (x2 - x1), y1 + ua * (y2 - y1)));
  }

  return results;
}

std::vector<MyVector> MyMath::intersectLineLineInfinite(const MyLine &l1, const MyLine &l2) {
  std::vector<MyVector> results;
  double x1 = l1.start.x, y1 = l1.start.y;
  double x2 = l1.end.x, y2 = l1.end.y;
  double x3 = l2.start.x, y3 = l2.start.y;
  double x4 = l2.end.x, y4 = l2.end.y;
  double denominator = (y4 - y3) * (x2 - x1) - (x4 - x3) * (y2 - y1);
  if (std::abs(denominator) < TOLERANCE) return results;
  double ua = ((x4 - x3) * (y1 - y3) - (y4 - y3) * (x1 - x3)) / denominator;
  results.push_back(MyVector(x1 + ua * (x2 - x1), y1 + ua * (y2 - y1)));
  return results;
}

std::vector<MyVector> MyMath::intersectLineCircle(const MyLine &line, const MyCircle &circle) {
    std::vector<MyVector> results;

    MyVector d = line.end - line.start;
    MyVector f = line.start - circle.center;

    double a = dotProduct(d, d);
    double b = 2 * dotProduct(f, d);
    double c = dotProduct(f, f) - circle.radius * circle.radius;

    double discriminant = b * b - 4 * a * c;

    // [핵심 수정 1] 선과 원이 정확히 접할 때 부동소수점 오차로 판별식이 미세한 음수가 되는 현상 보정
    if (discriminant < 0.0 && discriminant > -1e-7) {
        discriminant = 0.0;
    }

    if (discriminant < 0.0) {
        return results; // 교점 없음
    }

    discriminant = std::sqrt(discriminant);

    double t1 = (-b - discriminant) / (2 * a);
    double t2 = (-b + discriminant) / (2 * a);

    // t가 [0, 1] 범위 내에 있는지 확인 (선분 위에 있는지)
    if (t1 >= -TOLERANCE && t1 <= 1.0 + TOLERANCE) {
        results.push_back(line.start + d * t1);
    }

    if (t2 >= -TOLERANCE && t2 <= 1.0 + TOLERANCE) {
        // 중근이 아닌 경우에만 추가
        if (!isEqual(t1, t2)) {
            results.push_back(line.start + d * t2);
        }
    }

    return results;
}

std::vector<MyVector> MyMath::intersectCircleCircle(const MyCircle &c1,
                                                    const MyCircle &c2) {
  std::vector<MyVector> results;

  MyVector dVec = c2.center - c1.center;
  double d = dVec.magnitude();

  // 1. 원이 서로 너무 멀거나, 하나가 다른 하나 안에 포함됨, 또는 동심원
  if (d > c1.radius + c2.radius || d < std::abs(c1.radius - c2.radius) ||
      isZero(d)) {
    return results;
  }

  // 2. 교차점 계산 (두 원의 교점을 지나는 현의 방정식 활용)
  // a: c1 중심에서 현까지의 거리
  double a = (c1.radius * c1.radius - c2.radius * c2.radius + d * d) / (2 * d);

  // h: 현의 절반 길이 (피타고라스 정리)
  double h = std::sqrt(std::max(0.0, c1.radius * c1.radius - a * a));

  // P2: 현과 중심 연결선의 교점
  MyVector p2 = c1.center + dVec * (a / d);

  // 교점 좌표 계산
  double x2 = p2.x;
  double y2 = p2.y;
  double dx = c2.center.x - c1.center.x;
  double dy = c2.center.y - c1.center.y;

  // 첫 번째 교점
  double x3_1 = x2 + h * dy / d;
  double y3_1 = y2 - h * dx / d;
  results.push_back(MyVector(x3_1, y3_1));

  // 두 번째 교점 (중근인 경우가 아니라면)
  double x3_2 = x2 - h * dy / d;
  double y3_2 = y2 + h * dx / d;

  if (!isEqual(x3_1, x3_2) || !isEqual(y3_1, y3_2)) {
    results.push_back(MyVector(x3_2, y3_2));
  }

  return results;
}

std::vector<MyVector> MyMath::intersectLineArc(const MyLine &line,
                                               const MyArc &arc) {
  // 1. 호를 포함하는 원과 선분의 교점을 먼저 구함
  MyCircle tempCircle(arc.center, arc.radius);
  std::vector<MyVector> candidates = intersectLineCircle(line, tempCircle);
  std::vector<MyVector> finalResults;

  // 2. 구해진 교점들이 호의 각도 범위(Span) 내에 있는지 필터링
  for (const auto &pt : candidates) {
    double angle = std::atan2(pt.y - arc.center.y, pt.x - arc.center.x);
    angle = normalizeAngle(rad2deg(angle));

    if (isAngleBetween(angle, arc.startAngle, arc.spanAngle)) {
      finalResults.push_back(pt);
    }
  }

  return finalResults;
}

std::vector<MyVector> MyMath::intersectCircleArc(const MyCircle &circle,
                                                 const MyArc &arc) {
  MyCircle arcAsCircle(arc.center, arc.radius);
  std::vector<MyVector> candidates = intersectCircleCircle(circle, arcAsCircle);
  std::vector<MyVector> finalResults;

  for (const auto &pt : candidates) {
    double angle = std::atan2(pt.y - arc.center.y, pt.x - arc.center.x);
    angle = normalizeAngle(rad2deg(angle));
    if (isAngleBetween(angle, arc.startAngle, arc.spanAngle)) {
      finalResults.push_back(pt);
    }
  }
  return finalResults;
}

std::vector<MyVector> MyMath::intersectArcArc(const MyArc &a1,
                                              const MyArc &a2) {
  MyCircle c1(a1.center, a1.radius);
  MyCircle c2(a2.center, a2.radius);
  std::vector<MyVector> candidates = intersectCircleCircle(c1, c2);
  std::vector<MyVector> finalResults;

  for (const auto &pt : candidates) {
    double ang1 = normalizeAngle(
        rad2deg(std::atan2(pt.y - a1.center.y, pt.x - a1.center.x)));
    double ang2 = normalizeAngle(
        rad2deg(std::atan2(pt.y - a2.center.y, pt.x - a2.center.x)));

    if (isAngleBetween(ang1, a1.startAngle, a1.spanAngle) &&
        isAngleBetween(ang2, a2.startAngle, a2.spanAngle)) {
      finalResults.push_back(pt);
    }
  }
  return finalResults;
}

MyVector MyMath::reflectPoint(const MyVector &p, const MyVector &p1,
                              const MyVector &p2) {
  MyVector v = p2 - p1;
  double vLenSq = dotProduct(v, v);
  if (isZero(vLenSq))
    return p; // 직선이 점인 경우

  MyVector w = p - p1;
  // w의 v에 대한 투영 벡터의 계수 t (Projection)
  double t = dotProduct(w, v) / vLenSq;
  MyVector projection = v * t;
  MyVector perp = w - projection;

  // 대칭점 P' = P - 2 * perp
  return p - perp * 2.0;
}

MyVector MyMath::rotatePoint(const MyVector &p, const MyVector &base,
                             double angle) {
  double rad = deg2rad(angle);
  double cosA = std::cos(rad);
  double sinA = std::sin(rad);

  double dx = p.x - base.x;
  double dy = p.y - base.y;

  return MyVector(base.x + dx * cosA - dy * sinA,
                  base.y + dx * sinA + dy * cosA);
}

MyVector MyMath::scalePoint(const MyVector &p, const MyVector &base,
                            double scaleFactor) {
  return MyVector(base.x + (p.x - base.x) * scaleFactor,
                  base.y + (p.y - base.y) * scaleFactor);
}

std::vector<MyVector> MyMath::intersectLineSpline(const MyLine &line, const MySpline &spline) {
    std::vector<MyVector> pts;
    std::vector<MyVector> poly = spline.getPoints(20);
    for (size_t i = 0; i < poly.size() - 1; ++i) {
        std::vector<MyVector> hits = intersectLineLine(line, MyLine(poly[i], poly[i+1]));
        pts.insert(pts.end(), hits.begin(), hits.end());
    }
    return pts;
}

std::vector<MyVector> MyMath::intersectArcSpline(const MyArc &arc, const MySpline &spline) {
    std::vector<MyVector> pts;
    std::vector<MyVector> poly = spline.getPoints(20);
    for (size_t i = 0; i < poly.size() - 1; ++i) {
        std::vector<MyVector> hits = intersectLineArc(MyLine(poly[i], poly[i+1]), arc);
        pts.insert(pts.end(), hits.begin(), hits.end());
    }
    return pts;
}

std::vector<MyVector> MyMath::intersectSplineSpline(const MySpline &s1, const MySpline &s2) {
    std::vector<MyVector> pts;
    std::vector<MyVector> poly1 = s1.getPoints(20);
    std::vector<MyVector> poly2 = s2.getPoints(20);
    for (size_t i = 0; i < poly1.size() - 1; ++i) {
        MyLine L1(poly1[i], poly1[i+1]);
        for (size_t j = 0; j < poly2.size() - 1; ++j) {
            std::vector<MyVector> hits = intersectLineLine(L1, MyLine(poly2[j], poly2[j+1]));
            pts.insert(pts.end(), hits.begin(), hits.end());
        }
    }
    return pts;
}

std::vector<MyVector> MyMath::intersectPolylineSpline(const MyPolyline &polyline, const MySpline &spline) {
    std::vector<MyVector> pts;
    std::vector<MyVector> polyS = spline.getPoints(20);
    for (size_t i = 0; i < polyline.vertices.size() - 1; ++i) {
        MyLine L(polyline.vertices[i], polyline.vertices[i+1]);
        for (size_t j = 0; j < polyS.size() - 1; ++j) {
            std::vector<MyVector> hits = intersectLineLine(L, MyLine(polyS[j], polyS[j+1]));
            pts.insert(pts.end(), hits.begin(), hits.end());
        }
    }
    return pts;
}

std::vector<MyVector> MyMath::approximateSpline(const MySpline &spline, int segmentsPerSection) {
    return spline.getPoints(segmentsPerSection);
}

double MyMath::linearDimAngle(const MyVector &p1, const MyVector &p2, const MyVector &pos) {
  double ox = std::max(std::max(std::min(p1.x, p2.x) - pos.x, pos.x - std::max(p1.x, p2.x)), 0.0);
  double oy = std::max(std::max(std::min(p1.y, p2.y) - pos.y, pos.y - std::max(p1.y, p2.y)), 0.0);
  double spanX = std::abs(p2.x - p1.x);
  double spanY = std::abs(p2.y - p1.y);
  double angle;
  if (oy > ox) angle = 0.0;
  else if (ox > oy) angle = 90.0;
  else angle = (spanX >= spanY) ? 0.0 : 90.0;
  // 선택된 방향의 측정값이 0이면(두 점이 그 방향에 수직 정렬) 반대 방향으로 전환
  if (angle == 0.0 && spanX < 1e-6 && spanY >= 1e-6) angle = 90.0;
  else if (angle == 90.0 && spanY < 1e-6 && spanX >= 1e-6) angle = 0.0;
  return angle;
}
