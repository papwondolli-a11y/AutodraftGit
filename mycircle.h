#ifndef MYCIRCLE_H
#define MYCIRCLE_H

#include "myentity.h"
#include "myvector.h"

class MyLine; // 전방 선언

class MyCircle : public MyEntity {
public:
    MyVector center;
    double radius;

    MyCircle();
    MyCircle(const MyVector& c, double r);

    // [필수 구현]
    EntityType type() const override { return EntityType::Circle; }
    std::vector<MyVector> intersect(const MyEntity* other) const override;
    MyVector getNearestPoint(const MyVector& pos) const override;
    MyVector getPerpendicularPoint(const MyVector& base) const override;
};

#endif // MYCIRCLE_H
