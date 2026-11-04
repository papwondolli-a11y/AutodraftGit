#ifndef MYPOLYLINE_H
#define MYPOLYLINE_H

#include "myentity.h"
#include <vector>

class MyPolyline : public MyEntity {
public:
  std::vector<MyVector> vertices;
  std::vector<double> bulges; // [추가] 각 정점 사이의 곡률 (0이면 직선)
  bool isClosed;

  MyPolyline();
  MyPolyline(const std::vector<MyVector> &v, bool closed = false);

  EntityType type() const override { return EntityType::Polyline; }
  std::vector<MyVector> intersect(const MyEntity *other) const override;
  MyVector getNearestPoint(const MyVector &pos) const override;
};

#endif // MYPOLYLINE_H
