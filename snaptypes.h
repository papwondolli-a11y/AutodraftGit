#ifndef SNAPTYPES_H
#define SNAPTYPES_H

#include "myvector.h" // [중요] QPointF 대신 MyVector를 사용해야 함

enum class SnapType {
    None,
    Grid,
    Endpoint,
    Midpoint,
    Center,
    Quadrant,
    Intersection,
    Nearest,
    Perpendicular
};

struct SnapPoint {
    // [핵심 변경] 기존 QPointF -> MyVector로 변경
    // 이렇게 해야 정밀한 수학 계산이 가능하고, .toPointF() 함수도 쓸 수 있게 됩니다.
    MyVector pos;
    SnapType type;

    SnapPoint() : type(SnapType::None) {}
    SnapPoint(const MyVector& p, SnapType t) : pos(p), type(t) {}
};

#endif // SNAPTYPES_H
