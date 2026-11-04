#ifndef MYHOLE_H
#define MYHOLE_H

#include "myentity.h"
#include "myvector.h"
#include <QString>

// 콜아웃 텍스트 표기 방식 (카운터보어/카운터싱크 등)
enum class HoleSymbolStyle {
    Text,    // C/B, C/S, DP 등 텍스트 약어 (기본값)
    Graphic  // 그래픽 기호 (추후 확장, 폰트 임베딩 필요)
};

// 판재 모서리·대칭선에 걸친 구멍을 절반만 표시할 때 쓰는 렌더링 범위.
// 데이터(지름/탭가공/카운터보어 등)는 항상 완전한 값을 유지하며, 이 값은 오직
// 화면/출력에 그리는 범위만 제한한다 — trim처럼 실제로 잘라내지 않는다.
enum class HoleRenderMode {
    Full,  // 전체 - 완전한 원으로 렌더링
    Right, // 오른쪽 반원만 렌더링
    Left   // 왼쪽 반원만 렌더링
};

// 구멍(드릴/탭/카운터보어/카운터싱크) 데이터
class MyHole : public MyEntity {
public:
    MyVector position; // 중심점

    double diameter = 10.0; // 드릴/구멍 지름

    bool throughHole = true; // true=관통, false=막힌 구멍(블라인드)
    double depth = 0.0;      // 블라인드일 때 깊이

    bool isThreaded = false;
    QString threadSpec = "M10x1.5"; // 예: M10x1.5
    QString threadClass = "6H";     // 나사 등급
    // 탭의 관통 여부/드릴 깊이는 별도 필드로 두지 않음 — 기본구멍의 throughHole/depth가
    // 유일한 드릴 깊이 기준(탭은 그 구멍에 파는 것이므로 독립적일 수 없음).
    // threadDepth는 "그 드릴 구멍 중 실제로 나사산이 파인 깊이"만 의미하며, 막힌 구멍일 때만
    // 쓰이고 항상 depth 이하로 제한됨(관통이면 나사산도 전체 길이로 간주해 이 값을 안 씀).
    double threadDepth = 0.0;

    QString toleranceClass; // 예: H7 (리머/정밀구멍), 비어있으면 미표기

    bool hasCounterbore = false;
    double cbDiameter = 0.0;
    double cbDepth = 0.0;

    bool hasCountersink = false;
    double csDiameter = 0.0;
    double csAngle = 90.0;

    HoleSymbolStyle symbolStyle = HoleSymbolStyle::Text;

    bool showCenterLine = true; // 중심선(수평/수직) 생성 여부

    HoleRenderMode renderMode = HoleRenderMode::Full; // 전체/오른쪽/왼쪽 반원 표시

    // 그룹별 표시 레이어 (비어있으면 아이템의 기본 레이어/펜을 그대로 사용)
    QString baseLayer;      // 기본 구멍(원)
    QString threadLayer;    // 탭(나사) 내경선
    QString cbLayer;        // 카운터보어
    QString csLayer;        // 카운터싱크

    MyHole() {}
    virtual ~MyHole() {}

    EntityType type() const override { return EntityType::Hole; }

    std::vector<MyVector> intersect(const MyEntity *other) const override;
    MyVector getNearestPoint(const MyVector &pos) const override;

    // "M10x1.5" 형식의 threadSpec 문자열을 호칭지름/피치로 분해
    // 다이얼로그(탭드릴 자동계산)와 HoleItem(나사산 표시) 양쪽에서 공유
    static bool parseThreadSpec(const QString &spec, double &majorDiameter, double &pitch);

    // 탭 호칭지름(major diameter) 반지름 (threadSpec 파싱 실패 시 근사치로 대체)
    double threadMajorRadius() const;

    // 이 구멍의 규격에 맞는 카운터보어/카운터싱크 지름 제안값.
    // 탭 구멍이면 나사 호칭지름(ISO 4762 머리 카운터보어 / ISO 10642 접시머리 기준),
    // 아니면 HoleDialog와 같은 근사식(기본구멍 지름 × 1.8).
    double suggestedCbDiameter() const;
    double suggestedCsDiameter() const;

    // 기본구멍/나사호칭원/카운터보어/카운터싱크를 통틀어 가장 바깥쪽 반지름.
    // 중심선 길이 등 "이 구멍의 전체 크기"를 기준으로 삼아야 하는 곳에서 공용으로 사용.
    double outerRadius() const;
};

#endif // MYHOLE_H
