#ifndef MYARC_H
#define MYARC_H

#include "myentity.h"
#include "myvector.h"

class MyLine;

class MyArc : public MyEntity {
public:
    MyVector center;
    double radius;
    double startAngle; // Degree (0~360)
    double spanAngle;  // Degree (CCW 양수)

    MyArc();
    MyArc(const MyVector& c, double r, double start, double span);

    // [필수 구현]
    EntityType type() const override { return EntityType::Arc; }
    std::vector<MyVector> intersect(const MyEntity* other) const override;
    MyVector getNearestPoint(const MyVector& pos) const override;
    MyVector getPerpendicularPoint(const MyVector& base) const override;

    // 유틸리티
    MyVector startPoint() const;
    MyVector endPoint() const;
};

#endif // MYARC_H
