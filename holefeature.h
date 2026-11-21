#ifndef HOLEFEATURE_H
#define HOLEFEATURE_H

#include "myhole.h"
#include "revolveprofile.h"

#include <QList>
#include <QPointF>
#include <QString>

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
    // 이 쪽(좌측면 = true)에서 구멍 입구가 보이는가
    bool openToward(bool leftSideView) const;
};

#endif // HOLEFEATURE_H
