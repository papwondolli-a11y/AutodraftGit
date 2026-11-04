#include "myline.h"
#include "myarc.h"
#include "mycircle.h" // 교차점 계산 시 상대방 타입을 알아야 하므로 include
#include "mymath.h"   // 수학 공식 창고


MyLine::MyLine() : start(0, 0), end(0, 0) {}

MyLine::MyLine(const MyVector &s, const MyVector &e) : start(s), end(e) {}

std::vector<MyVector> MyLine::intersect(const MyEntity *other) const {
  if (!other)
    return {};

  switch (other->type()) {
  case EntityType::Line: {
    const MyLine *l = static_cast<const MyLine *>(other);
    return MyMath::intersectLineLine(*this, *l);
  }
  case EntityType::Circle: {
    const MyCircle *c = static_cast<const MyCircle *>(other);
    return MyMath::intersectLineCircle(*this, *c);
  }
  case EntityType::Arc: {
    const MyArc *a = static_cast<const MyArc *>(other);
    return MyMath::intersectLineArc(*this, *a);
  }
  case EntityType::Polyline:
  case EntityType::Ellipse: {
    return other->intersect(this);
  }
  default:
    return {};
  }
}

MyVector MyLine::getNearestPoint(const MyVector &pos) const {
  return MyMath::getNearestPointOnSegment(pos, *this);
}

MyVector MyLine::getPerpendicularPoint(const MyVector &base) const {
  return MyMath::getPerpendicularPointOnLine(base, *this);
}

double MyLine::length() const { return start.distanceTo(end); }
