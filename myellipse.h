#ifndef MYELLIPSE_H
#define MYELLIPSE_H

#include "myentity.h"
#include "myvector.h"
#include <vector>

// 타원 / 타원호 데이터 (DXF ELLIPSE 표현과 동일)
//   center      : 중심점
//   majorAxis   : 중심 → 장축 끝점 벡터 (길이 = 장축 반지름, 방향 = 장축 방향)
//   ratio       : 단축/장축 비율 (0 < ratio <= 1)
//   startParam  : 시작 매개각(라디안, CCW)
//   endParam    : 끝 매개각(라디안, CCW) — startParam < endParam <= startParam + 2π
//   P(t) = center + majorAxis*cos(t) + minorAxis*sin(t)
class MyEllipse : public MyEntity {
public:
  MyVector center;
  MyVector majorAxis;
  double ratio;
  double startParam;
  double endParam;

  MyEllipse();
  MyEllipse(const MyVector &c, const MyVector &major, double r,
            double startP = 0.0, double endP = 6.283185307179586);

  EntityType type() const override { return EntityType::Ellipse; }
  std::vector<MyVector> intersect(const MyEntity *other) const override;
  MyVector getNearestPoint(const MyVector &pos) const override;

  // 기하 유틸리티
  MyVector minorAxis() const;           // 중심 → 단축 끝점 벡터 (장축을 CCW 90° 회전 × ratio)
  double majorRadius() const { return majorAxis.magnitude(); }
  double minorRadius() const { return majorAxis.magnitude() * ratio; }
  double rotationRad() const;           // 장축의 X축 기준 각도
  bool isFull() const;                  // 전체 타원 여부
  double span() const { return endParam - startParam; }
  MyVector pointAt(double t) const;
  MyVector startPoint() const { return pointAt(startParam); }
  MyVector endPoint() const { return pointAt(endParam); }
  // 임의의 점이 가리키는 매개각 (타원 좌표계로 역변환)
  double paramAt(const MyVector &p) const;
  // 매개각 t 가 호 범위(start~end) 안에 있는지
  bool containsParam(double t) const;
  // 호 범위를 따라 샘플링한 점들 (전체 타원이면 닫힌 형태로 마지막 점=첫 점)
  std::vector<MyVector> samplePoints(int segments = 180) const;
  // 시작/끝 매개각 정규화 (start ∈ [0,2π), end ∈ (start, start+2π])
  void normalizeParams();
};

#endif // MYELLIPSE_H
