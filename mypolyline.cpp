#include "mypolyline.h"
#include "myline.h"
#include "mymath.h"


MyPolyline::MyPolyline() : isClosed(false) {}

MyPolyline::MyPolyline(const std::vector<MyVector> &v, bool closed)
    : vertices(v), isClosed(closed) {}

std::vector<MyVector> MyPolyline::intersect(const MyEntity *other) const {
  std::vector<MyVector> allIntersections;
  if (vertices.size() < 2)
    return allIntersections;

  // 폴리라인을 여러 개의 선분으로 나누어 각각 교차점 계산
  size_t count = isClosed ? vertices.size() : vertices.size() - 1;
  for (size_t i = 0; i < count; ++i) {
    MyVector v1 = vertices[i];
    MyVector v2 = vertices[(i + 1) % vertices.size()];
    MyLine segment(v1, v2);

    std::vector<MyVector> pts = segment.intersect(other);
    allIntersections.insert(allIntersections.end(), pts.begin(), pts.end());
  }

  // 중복 제거 (필요시 MyMath 등을 통해 정렬 및 유니크 처리 가능)
  return allIntersections;
}

MyVector MyPolyline::getNearestPoint(const MyVector &pos) const {
  if (vertices.empty())
    return pos;
  if (vertices.size() == 1)
    return vertices[0];

  MyVector nearest;
  double minDoc = -1.0;

  size_t count = isClosed ? vertices.size() : vertices.size() - 1;
  for (size_t i = 0; i < count; ++i) {
    MyVector v1 = vertices[i];
    MyVector v2 = vertices[(i + 1) % vertices.size()];
    MyLine segment(v1, v2);

    MyVector pt = segment.getNearestPoint(pos);
    double dist = pt.distanceTo(pos);

    if (minDoc < 0 || dist < minDoc) {
      minDoc = dist;
      nearest = pt;
    }
  }
  return nearest;
}
