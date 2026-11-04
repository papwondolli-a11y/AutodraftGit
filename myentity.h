#ifndef MYENTITY_H
#define MYENTITY_H

#include "myvector.h"
#include <vector>

enum class EntityType {
  Line,
  Circle,
  Arc,
  Polyline,
  Text,
  Dimension,
  Leader,
  Hatch,
  Gdt,       // 기하공차 (GD&T) 프레임
  SurfaceFinish, // 표면 거칠기 기호
  Spline,
  Hole,      // 구멍 (드릴/탭/카운터보어/카운터싱크)
  Ellipse,   // 타원 / 타원호
};

class MyEntity {
public:
  virtual ~MyEntity() {}

  // 1. 타입 식별
  virtual EntityType type() const = 0;

  // 2. [다형성 핵심] 나와 상대방(other)의 교차점 계산
  // 구현은 각 자식 클래스(.cpp)에서 MyMath를 호출하여 수행
  virtual std::vector<MyVector> intersect(const MyEntity *other) const = 0;

  // 3. 나에게서 가장 가까운 점 (Nearest Snap)
  virtual MyVector getNearestPoint(const MyVector &pos) const = 0;

  // 4.  (Perpendicular Snap)
  virtual MyVector getPerpendicularPoint(const MyVector &base) const {
    return getNearestPoint(base);
  }

  // 공통 속성
  int id = -1;
  unsigned int color = 0xFFFFFF;
  int layerId = 0;
};

#endif // MYENTITY_H
