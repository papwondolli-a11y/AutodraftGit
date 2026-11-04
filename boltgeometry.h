#ifndef BOLTGEOMETRY_H
#define BOLTGEOMETRY_H

#include <QPointF>
#include <QList>
#include <QPainterPath>

class QGraphicsItem;
class CadItem;
class ArcItem;

// ─────────────────────────────────────────────────────────────────────────
// boltgeometry.h — BoltTool/WrenchBoltTool/HHBoltTool/HBBoltTool 4개 볼트 배치
// 툴에 중복돼 있던 순수 형상 자료구조 + 헬퍼 함수를 한곳에 모은 것.
//
// 각 툴의 buildLocalSegments/buildLocalArcs/buildLocalCircles(Lisp 이식 알고리즘,
// 볼트 종류마다 완전히 다른 실제 계산식)는 이 파일과 무관하게 각 tool.cpp에 그대로
// 남아있다 — 여기서 공유하는 건 "계산된 형상을 어떻게 그릴지"이지 "형상을 어떻게
// 계산할지"가 아니다.
//
// BoltLineKind::Phantom + isThreadDiaLayer 등은 BoltTool(육각볼트)만 실제로 쓰고
// 나머지 3개 툴은 항상 기본값(Object / false)만 쓴다 — BoltTool의 상위집합 형태를
// 공용 타입으로 채택한 것뿐, 나머지 툴에 별도 분기가 필요하진 않다.
// ─────────────────────────────────────────────────────────────────────────

// 선분/원/호의 선 종류 — 그릴 때 펜 스타일을 구분하기 위함
enum class BoltLineKind {
    Object,          // 실선 (외형선)
    Centerline,      // 1점쇄선 (중심선)
    ThreadIndicator, // 가는 실선 (나사 골지름 표시선 — Lisp 원본에서도 실선, 굵기만 얇음)
    Phantom          // 2점쇄선 (가상선 — BoltTool의 원형 러벤도 대변원 등 실제로 보이지 않는 참조선)
};

// 볼트 기호를 구성하는 선분 하나
// isThreadDiaLayer: true면 "골지름선" 레이어로, false면 "외형선" 레이어로 배치된다.
struct BoltSegment {
    QPointF      a;
    QPointF      b;
    BoltLineKind kind;
    bool         isThreadDiaLayer = false;
};

// 볼트 기호를 구성하는 호(arc) 하나 — center를 중심으로 a,b 두 점을 지나는 원호 중
// 짧은 쪽(단원호)을 그린다.
struct BoltArcSpec {
    QPointF      center;
    QPointF      a;
    QPointF      b;
    BoltLineKind kind = BoltLineKind::Object;
    bool         isThreadDiaLayer = false;
};

// 볼트 기호를 구성하는 원 하나 (원형 러벤도의 머리 외곽원 등)
struct BoltCircleSpec {
    QPointF      center;
    double       radius;
    BoltLineKind kind = BoltLineKind::Object;
};

// QGraphicsItem::setData()/data() 에 쓰는 공용 키 — 골지름선/중심선/가상선 레이어 소속
// 여부를 buildBoltGraphicsItems()에서 각 툴의 placeItemAndReset()으로 전달하는 용도.
// 각 툴은 자기가 방금 만든 아이템에서만 이 키를 읽으므로 툴마다 다른 리터럴을 쓸
// 이유가 없어 하나로 통일했다.
extern const int kBoltThreadDiaLayerDataKey;
extern const int kBoltCenterlineLayerDataKey;
extern const int kBoltPhantomLayerDataKey;

// Lisp의 polar(pt, angleDeg, dist)로 계산한 좌표를 우리 로컬 좌표계로 옮기는 변환.
// Lisp_Y → v = -Lisp_Y, X는 그대로 u. (모든 볼트 툴이 공유하는 규약)
QPointF fromLisp(double lispX, double lispY);

// 세 점 A,B,C 를 모두 지나는 원의 외접원 중심을 구한다 (표준 공식).
// A,B,C가 일직선이면 원을 만들 수 없으므로 false를 반환한다.
bool circumcenter3(const QPointF &A, const QPointF &B, const QPointF &C, QPointF &outCenter);

// 로컬 좌표(u,v)를 기준점(origin)/각도(angleDeg)로 world 좌표로 변환.
// 로컬 u(가로) → world "오른쪽" 벡터(F를 시계방향으로 90도 회전), 로컬 v(세로) → world 방향 F.
QPointF localToWorld(const QPointF &local, const QPointF &origin, double angleDeg);

// 선 종류(BoltLineKind)에 맞춰 펜 스타일을 세팅한다. 색상/선종류는 호출부(각 툴의
// placeItemAndReset)에서 레이어 펜으로 다시 덮어쓰므로 여기서는 골지름선 굵기(얇게)만 정한다.
void applyBoltLineKindStyle(CadItem *item, BoltLineKind kind);

// 중심(center)을 기준으로 a→b 로 이어지는 두 원호(단원호/장원호) 중 짧은 쪽으로
// ArcItem을 만든다. ArcItem(center,start,end) 생성자는 항상 start→end CCW로 그리므로,
// 필요하면 시작/끝을 바꿔서 짧은 쪽이 그려지도록 한다.
ArcItem *makeMinorBoltArcItem(const QPointF &center, const QPointF &a, const QPointF &b);

// 로컬 좌표계의 형상 목록(segments/arcs/circles)을 기준점(origin)/각도(angleDeg)로
// world 좌표로 변환한 QPainterPath를 만든다 (고스트 미리보기용). 아크는 Qt의 arcTo()
// 각도 규약(화면 좌표계 기준)에 의존하지 않도록, 로컬 좌표계에서 직접 각도를 보간해
// world로 변환한 점들을 이어 근사한다.
QPainterPath buildBoltWorldPath(const QList<BoltSegment> &segments,
                                const QList<BoltArcSpec> &arcs,
                                const QList<BoltCircleSpec> &circles,
                                const QPointF &origin, double angleDeg);

// 로컬 좌표계의 형상 목록을 기준점(origin)/각도(angleDeg)로 world 좌표에 배치해 실제
// LineItem/ArcItem/CircleItem 목록으로 만든다. layerName/pen은 호출부에서 적용한다.
// gripsEnabled: BoltTool(육각볼트)만 true — 나머지 3개 툴은 인접한 소켓 기호 끝점이
// 옆 도형 그립으로 잘못 잡히는 문제 때문에 false로 그립 편집을 꺼둔다(기존 동작 유지).
QList<QGraphicsItem *> buildBoltGraphicsItems(const QList<BoltSegment> &segments,
                                              const QList<BoltArcSpec> &arcs,
                                              const QList<BoltCircleSpec> &circles,
                                              const QPointF &origin, double angleDeg,
                                              bool gripsEnabled = false);

#endif // BOLTGEOMETRY_H
