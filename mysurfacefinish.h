#ifndef MYSURFACEFINISH_H
#define MYSURFACEFINISH_H

#include "myentity.h"
#include "myvector.h"
#include <QString>
#include <QList>

// 표면 거칠기 기호 종류
enum class SurfaceFinishSymbol {
    Basic               = 0, // 기본 기호 (가공방법 불문)
    MachiningProhibited = 2, // 제거가공 금지 (원 추가)
    LegacyGrade         = 3, // 구 KS 다듬질 기호 (▽ 개수로 등급 표시)
    TriangleFlag        = 4  // 짧은 다리 자리에 삼각형이 붙는 변형 기호
};

struct SurfaceFinishData : public MyEntity {
    SurfaceFinishSymbol symbol = SurfaceFinishSymbol::Basic;

    QString raValue;            // Ra/Rz 값, 예: "3.2" (위치 a)
    QString raLetterCode;       // 비어있으면 "RA"(숫자 값) 모드, "W"/"X"/"Y"/"Z"면
                                 // 값 대신 해당 글자 하나만 표시(구 KS 등급 약식 표기)
    QString productionMethod;   // 가공방법·표면처리, 예: "밀링" (위치 b)
    QString cutoffValue;        // 컷오프값/기준길이 (위치 c)
    QString laySymbol;          // 줄무늬 방향 기호, 예: "=", "X" (위치 d)
    QString machiningAllowance; // 기계가공 여유(mm) (위치 e)

    int legacyGrade = 2;        // symbol == LegacyGrade 일 때 1~4 (거친/중/상/정밀다듬질)
    bool showTailBar = false;   // "꼬리선": 긴 다리 끝에 막대(제거가공 필요 표시)를 추가할지 여부.
                                 // LegacyGrade를 제외한 모든 기호 종류에 독립적으로 적용 가능

    MyVector        position;      // 지시선 없을 때 배치 기준점 / 지시선 있을 때 심볼 꼭짓점
    QList<MyVector> leaderPoints;  // 지시선 꼭짓점들
    double          rotation = 0.0;
    double          textHeight = 2.5;
    bool            useLeader = true;
    QString         fontName;

    QString symbolLayer; // 기호(체크마크/삼각형/원 등) 전용 레이어 (비어있으면 자신의 레이어 사용)
    QString textLayer;   // 값 문자 전용 레이어 (비어있으면 자신의 레이어 사용)

    SurfaceFinishData()
        : fontName("Arial")
    {}

    EntityType type() const override { return EntityType::SurfaceFinish; }

    std::vector<MyVector> intersect(const MyEntity * /*other*/) const override {
        return {};
    }

    MyVector getNearestPoint(const MyVector &pos) const override {
        return pos;
    }
};

#endif // MYSURFACEFINISH_H
