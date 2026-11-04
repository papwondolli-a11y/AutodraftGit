#include "mycircle.h"
#include "myarc.h"
#include "myline.h" // 상대방이 Line일 경우를 대비
#include "mymath.h"


MyCircle::MyCircle() : center(0, 0), radius(0.0) {}

MyCircle::MyCircle(const MyVector &c, double r) : center(c), radius(r) {}

std::vector<MyVector> MyCircle::intersect(const MyEntity *other) const {
  if (!other)
    return {};

  switch (other->type()) {
  case EntityType::Line: {
    // Line vs Circle (순서를 바꿔서 MyMath 호출)
    const MyLine *l = static_cast<const MyLine *>(other);
    return MyMath::intersectLineCircle(*l, *this);
  }
  case EntityType::Circle: {
    // Circle vs Circle
    const MyCircle *c = static_cast<const MyCircle *>(other);
    return MyMath::intersectCircleCircle(*this, *c);
  }
  case EntityType::Arc: {
    const MyArc *a = static_cast<const MyArc *>(other);
    return MyMath::intersectCircleArc(*this, *a);
  }
  case EntityType::Polyline:
  case EntityType::Ellipse: {
    return other->intersect(this);
  }
  default:
    return {};
  }
}

MyVector MyCircle::getNearestPoint(const MyVector &pos) const {
  return MyMath::getNearestPointOnCircle(pos, *this);
}

MyVector MyCircle::getPerpendicularPoint(const MyVector &base) const {
  return MyMath::getPerpendicularPointOnCircle(base, *this);
}
