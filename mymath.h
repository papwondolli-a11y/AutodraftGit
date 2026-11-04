#ifndef MYMATH_H
#define MYMATH_H

#include <vector>

// 전방 선언 (순환 참조 방지 및 컴파일 속도 향상)
class MyVector;
class MyLine;
class MyCircle;
class MyArc;
class MySpline;
class MyPolyline;

class MyMath {
public:
  // 기하학적 오차 범위 (허용치)
  static const double TOLERANCE;
  static const double PI;

  // =========================================================
  // 1. 기본 유틸리티 & 벡터 연산
  // =========================================================
  static bool isEqual(double a, double b, double tol = TOLERANCE);
  static bool isZero(double a, double tol = TOLERANCE);

  // 각도 변환 및 정규화
  static double deg2rad(double deg);
  static double rad2deg(double rad);
  static double normalizeAngle(double angle); // 0.0 <= angle < 360.0

  // 각도가 특정 범위(Arc Span) 내에 있는지 확인 (CCW 기준)
  static bool isAngleBetween(double angle, double start, double span);

  // 벡터 내적 (Dot Product)
  static double dotProduct(const MyVector &v1, const MyVector &v2);
  // 선형치수 수평(0)/수직(90) 판정: 치수선 위치가 두 점 바운딩박스 밖으로 벗어난 축 기준
  static double linearDimAngle(const MyVector &p1, const MyVector &p2, const MyVector &pos);

  // 2D 외적 (Cross Product - Z component)
  static double crossProduct2D(const MyVector &v1, const MyVector &v2);

  // =========================================================
  // 2. 단순 스냅 도우미 (Grid, Midpoint, Quadrant)
  // =========================================================
  static MyVector snapToGrid(const MyVector &pos, double gridSize);
  static MyVector getMidpoint(const MyVector &p1, const MyVector &p2);

  // 원의 4분점 반환
  static std::vector<MyVector> getQuadrants(const MyCircle &circle);

  // =========================================================
  // 3. 근처 점(Nearest Snap) - Projection 알고리즘
  // =========================================================
  static MyVector getNearestPointOnSegment(const MyVector &pos, const MyLine &line);
  static MyVector getNearestPointOnCircle(const MyVector &pos, const MyCircle &circle);
  static MyVector getNearestPointOnArc(const MyVector &pos, const MyArc &arc);
  static MyVector getPerpendicularPointOnLine(const MyVector &pos, const MyLine &line);
  static MyVector getPerpendicularPointOnCircle(const MyVector &pos, const MyCircle &circle);
  static MyVector getPerpendicularPointOnArc(const MyVector &pos, const MyArc &arc);

  // =========================================================
  // 4. 교차점(Intersection) 계산
  // =========================================================
  static std::vector<MyVector> intersectLineLine(const MyLine &l1, const MyLine &l2);
  static std::vector<MyVector> intersectLineLineInfinite(const MyLine &l1, const MyLine &l2);
  static std::vector<MyVector> intersectLineCircle(const MyLine &line, const MyCircle &circle);
  static std::vector<MyVector> intersectCircleCircle(const MyCircle &c1, const MyCircle &c2);
  static std::vector<MyVector> intersectLineArc(const MyLine &line, const MyArc &arc);
  static std::vector<MyVector> intersectCircleArc(const MyCircle &circle, const MyArc &arc);
  static std::vector<MyVector> intersectArcArc(const MyArc &a1, const MyArc &a2);

  // Spline 관련 교차점 (근사화 기반)
  static std::vector<MyVector> intersectLineSpline(const MyLine &line, const MySpline &spline);
  static std::vector<MyVector> intersectArcSpline(const MyArc &arc, const MySpline &spline);
  static std::vector<MyVector> intersectSplineSpline(const MySpline &s1, const MySpline &s2);
  static std::vector<MyVector> intersectPolylineSpline(const MyPolyline &polyline, const MySpline &spline);

  // Spline 근사화 (폴리라인 변환용)
  static std::vector<MyVector> approximateSpline(const MySpline &spline, int segmentsPerSection = 10);

  // =========================================================
  // 5. 변형(Transformation)
  // =========================================================
  static MyVector reflectPoint(const MyVector &p, const MyVector &p1, const MyVector &p2);
  static MyVector rotatePoint(const MyVector &p, const MyVector &base, double angle);
  static MyVector scalePoint(const MyVector &p, const MyVector &base, double scaleFactor);
};

#endif // MYMATH_H
