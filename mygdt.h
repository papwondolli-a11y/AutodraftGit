#ifndef MYGDT_H
#define MYGDT_H

#include "myentity.h"
#include "myvector.h"
#include <QString>
#include <QList>

// GD&T 기하 특성 기호 열거형
enum class GdtSymbol {
    Straightness     = 0,   // 직진도   ─
    Flatness         = 1,   // 평면도   □
    Circularity      = 2,   // 진원도   ○
    Cylindricity     = 3,   // 원통도
    LineProfile      = 4,   // 선의 윤곽도
    SurfaceProfile   = 5,   // 면의 윤곽도
    Parallelism      = 6,   // 평행도   ∥
    Perpendicularity = 7,   // 직각도   ⊥
    Angularity       = 8,   // 경사도   ∠
    Position         = 9,   // 위치도   ⊕
    Concentricity    = 10,  // 동축도   ◎
    Symmetry         = 11,  // 대칭도   ≡
    CircularRunout   = 12,  // 원주 흔들림
    TotalRunout      = 13   // 전체 흔들림
};

struct GdtData : public MyEntity {
    GdtSymbol       symbol;
    QString         toleranceValue; // 예: "0.011", "Ø0.05"
    bool            hasDiameter;    // Ø 접두어 여부
    QList<QString>  datumRefs;      // 데이텀 A, B, C 등 (최대 3개)
    MyVector        position;       // 프레임 좌측 중앙 기준 배치점
    double          textHeight;     // 문자 높이 (기본 2.5)
    QList<MyVector> leaderPoints; // 지시선 꼭짓점들
    double rotation = 0.0;        // 기하공차 프레임 회전 각도
    bool useLeader = true;
    QString fontName;

    GdtData()
        : symbol(GdtSymbol::Circularity)
        , hasDiameter(false)
        , textHeight(2.5)
        , fontName("Arial")
    {}

    EntityType type() const override { return EntityType::Gdt; }

    std::vector<MyVector> intersect(const MyEntity * /*other*/) const override {
        return {};
    }

    MyVector getNearestPoint(const MyVector &pos) const override {
        return pos;
    }
};

#endif // MYGDT_H
