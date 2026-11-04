#ifndef MYSPLINE_H
#define MYSPLINE_H

#include "myentity.h"
#include <vector>

class MySpline : public MyEntity {
public:
  std::vector<MyVector> controlPoints;
  std::vector<double> knots;
  int degree;
  bool isClosed;

  MySpline();
  MySpline(const std::vector<MyVector> &cp, int d = 3, bool closed = false);

  std::vector<MyVector> getPoints(int segmentsPerSegment = 10) const;

  EntityType type() const override { return EntityType::Spline; }
  std::vector<MyVector> intersect(const MyEntity *other) const override;
  MyVector getNearestPoint(const MyVector &pos) const override;
};

#endif // MYSPLINE_H
