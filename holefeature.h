#ifndef HOLEFEATURE_H
#define HOLEFEATURE_H

#include "myhole.h"
#include "revolveprofile.h"

#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <QVector3D>
#include <vector>

class QGraphicsItem;

// ─────────────────────────────────────────────────────────────────────────────
// HoleFeature — 단면 회전체의 축 방향 구멍 (그리기 탭 구멍R·구멍P 로 측면도에 뚫은 HoleItem)
//
//   저장하지 않고 매번 도면에서 찾는다: 투상도로 만든 좌·우측면도 안에 있는 HoleItem = 그 부품의 구멍.
//   → 구멍을 옮기거나 지우거나 속성을 바꾸면 3D·다른 투상도·정면 단면 구멍이 따라간다.
//   부품 로컬 좌표: x = 축 방향, y = 정면도 위쪽, z = 보는 사람 쪽(정면도 앞).
//   좌측면도 오른쪽 = +z, 우측면도 오른쪽 = −z, 평면도 위쪽 = −z.
//   깊이: MyHole.throughHole 이면 관통, 아니면 그 구멍을 그린 측면 쪽 끝면에서 depth.
// ─────────────────────────────────────────────────────────────────────────────
struct HoleFeature {
    MyHole hole;          // 구멍 스펙 (지름·관통/깊이·탭·카운터보어·카운터싱크)
    QString groupId;      // HoleItem 의 HOLE_DATA_GROUP_ID (구멍 단면 태그와 같은 값)
    QString viewId;       // 구멍을 그린 측면도 (그 뷰에는 다시 그리지 않음)
    bool leftSide = true; // 좌측면도에 그린 구멍인가
    double y = 0.0;
    double z = 0.0;

    double radiusFromAxis() const;
    bool throughHole() const { return hole.throughHole; }
    // 단면에서 이 구멍이 지나는 x 범위 (재료가 있는 곳 기준). 재료가 없으면 false
    bool xRange(const QList<RevolveLoop> &loops, double &x0, double &x1) const;
    // 구멍 중심 높이에서 재료가 있는 전체 x 범위 [lo, hi] (관통·막힘과 무관). 재료가 없으면 false
    bool materialRange(const QList<RevolveLoop> &loops, double &lo, double &hi) const;
    // 입구에서 잰 드릴 깊이 (원뿔 끝 제외). 관통 = thickness, 막힘 = depth(0 이면 지름),
    // 막힌 탭 = 나사 깊이 + 탭 드릴 여유 — 구멍 단면 생성기·3D 커터와 같은 규칙
    double drillDepth(double thickness) const;

    // 구멍 단면 모양 (t = 입구에서 안쪽 깊이, r = 반지름) — 축 위 (-over, 0) 에서 시작해 축 위에서 끝나는 닫힌 단면.
    //   카운터보어 단 → 카운터싱크 원뿔(입구 밖으로 over 만큼 연장) → 드릴(관통 = thickness + over, 막힘 = drillDepth)
    //   → 막힘이면 118° 드릴 끝 원뿔. 탭은 드릴(골 쪽) 지름까지만 (나사산은 별도).
    //   headDepth = 카운터보어·카운터싱크가 끝나는 깊이(탭 나사 시작). 3D 커터·정면 해칭 빼기 공용.
    static std::vector<QPointF> cutProfile(const MyHole &hole, double thickness, double over, double *headDepth = nullptr);
    // 정면 단면에서 이 구멍이 차지하는 영역 (top = 입구 중심, dir = 안쪽 방향 단위벡터, thickness = 재료 두께)
    QPainterPath frontArea(const QPointF &top, const QPointF &dir, double thickness) const;
    // 이 쪽(좌측면 = true)에서 구멍 입구가 보이는가
    bool openToward(bool leftSideView) const;
};

// ─────────────────────────────────────────────────────────────────────────────
// SectionHole — 회전체 정면 단면 위에 "구멍 단면" 도구로 직접 그린 구멍 (측면도 연동으로 그려진 단면은 제외)
//   구멍 단면 선의 태그(원본 구멍 id·입구 TOP·방향 DIR·재료 두께)로 읽는다 — 매번 도면에서 찾음.
//   axial  = 축과 나란히 파고듦 (절단면 위, 단면에 보이는 그 자리 한 개)
//   radial = 바깥 둘레에서 축 쪽으로 파고듦 (반경 방향 구멍)
// ─────────────────────────────────────────────────────────────────────────────
struct SectionHole {
    MyHole hole;
    QString groupId;
    bool axial = true;
    double x = 0.0;          // 입구의 축 방향 위치 (축 좌표)
    double s = 0.0;          // 입구의 정면 높이 (축에서 위쪽 +)
    double dirSign = 1.0;    // axial: +1 = +x 쪽으로 / radial: 높이가 줄어드는 쪽(축 쪽) = -sign(s)
    double thickness = 0.0;  // 구멍 단면의 재료 두께 (관통 깊이)
};

// ─────────────────────────────────────────────────────────────────────────────
// PlateHole — 단독 돌출(판·블록)의 구멍 한 개 (도면 X·Y + 보는 쪽 Z 좌표)
//   정면(앞면)·측면도(옆면)·평면도(윗면)에 놓은 구멍R·구멍P, 정면에 구멍 단면 도구로 그린 옆면 구멍(두께 가운데)
// ─────────────────────────────────────────────────────────────────────────────
struct PlateHole {
    MyHole hole;
    QVector3D entry;           // 입구 중심 (X, Y, Z)
    QVector3D dir;             // 파고드는 방향 (단위)
    double thickness = 0.0;    // 입구에서 그 방향 재료 두께 (관통 깊이)
    QString viewId;            // 구멍R·구멍P 를 놓은 뷰 (정면 = 빈 문자열) — 그 뷰에는 다시 그리지 않음
    bool fromSection = false;  // 정면 구멍 단면 도구로 그린 구멍 (정면에는 사용자가 그린 선이 있음)
    const QGraphicsItem *item = nullptr;   // 구멍R·구멍P 객체 (투상도 숨기기에서 함께 숨김), 구멍 단면이면 없음
};

#endif // HOLEFEATURE_H
