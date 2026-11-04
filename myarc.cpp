#include "myarc.h"
#include "mycircle.h"
#include "myline.h"
#include "mymath.h"
#include <QtMath> // qDegreesToRadians 사용 (또는 cmath 사용)

MyArc::MyArc() : center(0, 0), radius(0), startAngle(0), spanAngle(0) {}

MyArc::MyArc(const MyVector &c, double r, double start, double span)
    : center(c), radius(r), startAngle(start), spanAngle(span) {}

std::vector<MyVector> MyArc::intersect(const MyEntity *other) const {
  if (!other)
    return {};

  switch (other->type()) {
  case EntityType::Line: {
    const MyLine *l = static_cast<const MyLine *>(other);
    // Line vs Arc
    return MyMath::intersectLineArc(*l, *this);
  }
  case EntityType::Circle: {
    // Arc vs Circle -> 보통 교차점은 최대 2개
    // 원과 원의 교차점을 구한 뒤, 그 점이 내 각도 범위(Span) 안에 있는지 체크
    const MyCircle *c = static_cast<const MyCircle *>(other);
    MyCircle myAsCircle(center, radius);

    std::vector<MyVector> candidates =
        MyMath::intersectCircleCircle(myAsCircle, *c);
    std::vector<MyVector> finalPoints;

    for (const auto &pt : candidates) {
      double angle = std::atan2(pt.y - center.y, pt.x - center.x);
      angle = MyMath::rad2deg(angle); // 라디안 -> 도
      if (MyMath::isAngleBetween(angle, startAngle, spanAngle)) {
        finalPoints.push_back(pt);
      }
    }
    return finalPoints;
  }
  case EntityType::Arc: {
    const MyArc *a = static_cast<const MyArc *>(other);
    return MyMath::intersectArcArc(*this, *a);
  }
  case EntityType::Polyline:
  case EntityType::Ellipse: {
    return other->intersect(this);
  }
  default:
    return {};
  }
}

MyVector MyArc::getNearestPoint(const MyVector &pos) const {
  return MyMath::getNearestPointOnArc(pos, *this);
}

MyVector MyArc::getPerpendicularPoint(const MyVector &base) const {
  return MyMath::getPerpendicularPointOnArc(base, *this);
}

MyVector MyArc::startPoint() const {
  double rad = MyMath::deg2rad(startAngle);
  return center + MyVector(std::cos(rad) * radius, std::sin(rad) * radius);
}

MyVector MyArc::endPoint() const {
  double rad = MyMath::deg2rad(startAngle + spanAngle);
  return center + MyVector(std::cos(rad) * radius, std::sin(rad) * radius);
}
