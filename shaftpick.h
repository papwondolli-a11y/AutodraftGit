#ifndef SHAFTPICK_H
#define SHAFTPICK_H

#include "shaftdata.h"

#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <QStringList>

class MyGraphicsScene;
class QGraphicsItem;

// ─────────────────────────────────────────────────────────────────────────────
// ShaftPick — 축(MechShaft)을 클릭하면 클릭한 지점의 구간 정보 (선택 도구에서 부름)
//   클릭 점을 축선에 투영 → 왼쪽 끝에서의 거리 → 구간 번호. 그 구간의 지름·공차(편차)·길이·위치·키홈·모따기·나사.
//   ① 도면: 그 구간 띠를 강조하고 말풍선(Ø·공차·구간)을 띄움 (오버레이, 다른 곳을 클릭하거나 Esc 로 사라짐)
//   ② 명령창: 한 줄 기록   ③ 속성 창: 선택 지점 + 구간 표 (last() 로 읽음)
// ─────────────────────────────────────────────────────────────────────────────
struct ShaftPickInfo {
    bool ok = false;
    QString uuid;
    QString name;
    ShaftParams params;
    int seg = -1;              // 클릭한 구간 (0부터)
    double xFrom = 0.0;        // 그 구간의 왼쪽 끝에서 거리 범위
    double xTo = 0.0;
    QString headline;          // "Ø30 h6 · 구간 3"
    QStringList details;       // 지름(편차)·길이·위치·키홈·모따기·나사
    QString commandLine;       // 명령창 한 줄
    QPainterPath highlight;    // 구간 띠 (도면)
    QPointF balloonAt;         // 말풍선 위치 (도면)
};

class ShaftPick {
public:
    // 클릭한 도형이 축이면 그 지점 정보. 축이 아니면 false
    static bool pick(MyGraphicsScene *scene, QGraphicsItem *item, const QPointF &pos, ShaftPickInfo &out);
    // 구간 한 줄 요약 ("Ø30 h6 · 길이 40 · 키홈 8×7")
    static QString segmentSummary(const ShaftParams &p, int i);
    // 지름 + 공차 (편차) 글
    static QString diameterText(const ShaftSegment &sg, bool withDeviation);
    // 도면에 강조·말풍선 표시 / 지우기
    static void showOverlay(MyGraphicsScene *scene, const ShaftPickInfo &info);
    static void clearOverlay(MyGraphicsScene *scene);

    // 마지막으로 클릭한 축 지점 (속성 창이 읽음)
    static const ShaftPickInfo &last();
    static void setLast(const ShaftPickInfo &info);
    static void clearLast();
};

#endif // SHAFTPICK_H
