#ifndef MYLINE_H
#define MYLINE_H

#include "myentity.h"
#include "myvector.h"

// 순환 참조 방지를 위한 전방 선언
class MyCircle;
class MyArc;

class MyLine : public MyEntity {
public:
    MyVector start;
    MyVector end;

    MyLine();
    MyLine(const MyVector& s, const MyVector& e);

    // [필수 구현]
    EntityType type() const override { return EntityType::Line; }

    // 교차점 계산 (Double Dispatching 역할)
    std::vector<MyVector> intersect(const MyEntity* other) const override;

    // 근처 점 계산
    MyVector getNearestPoint(const MyVector& pos) const override;

    // [신규] 수선의 발 계산
    MyVector getPerpendicularPoint(const MyVector& base) const override;

    // 자체 유틸리티
    double length() const;
};

#endif // MYLINE_H
