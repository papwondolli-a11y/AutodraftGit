#ifndef HOLECALLOUTFORMATTER_H
#define HOLECALLOUTFORMATTER_H

#include "myhole.h"
#include <QList>
#include <QString>

// MyHole 데이터로부터 콜아웃(지시선) 텍스트를 조립하는 순수 함수 모음.
// AutoCAD Mechanical의 Hole Callout과 동일한 철학 — 평면도에 여러 겹 도형을 그리는
// 대신 텍스트 한 줄(또는 몇 줄)로 가공 정보를 전달. 텍스트 약어 표기(C/B, C/S, DP)
// 고정 — 그래픽 기호(symbolStyle=Graphic)는 아직 미구현.
class HoleCalloutFormatter {
public:
    // 구멍 하나의 콜아웃 텍스트. 줄바꿈으로 구분된 여러 줄일 수 있음
    // (기본선 + 카운터보어 줄 + 카운터싱크 줄).
    static QString formatSingle(const MyHole &hole);

    // 여러 구멍을 하나의 콜아웃으로 묶음(예: "4x Ø10.5 THRU"). 스펙이 서로 다르면
    // 잘못된 다중선택으로 보고 ok=false를 반환(호출부가 경고 후 중단해야 함).
    static QString formatGrouped(const QList<MyHole> &holes, bool &ok);

    // formatSingle/formatGrouped의 역변환 — 사용자가 편집한 콜아웃 텍스트를 구멍 스펙으로 해석.
    // base의 위치·레이어·표시 옵션 등 텍스트에 없는 값은 그대로 유지하고 스펙 필드만 교체해 out에 담는다.
    // count는 첫 줄의 "Nx " 접두어(없으면 1). 해석 불가/모순된 값이면 false + error 메시지.
    static bool parse(const QString &text, const MyHole &base, MyHole &out, int &count, QString &error);

    static bool specsMatch(const MyHole &a, const MyHole &b);
};

#endif // HOLECALLOUTFORMATTER_H
