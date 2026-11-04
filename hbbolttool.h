#ifndef HBBOLTTOOL_H
#define HBBOLTTOOL_H

#include "abstractboltplacementtool.h"
#include "boltgeometry.h"
#include "mechpartstandards.h"

#include <QPointF>
#include <QList>
#include <QString>
#include <QPainterPath>

class QGraphicsItem;

// ─────────────────────────────────────────────────────────────────────────
// HBBoltTool — 둥근머리렌치볼트(육각구멍붙이 둥근머리볼트, 버튼헤드 소켓캡스크류)
// 정면도 간략 기호를 배치하는 툴. Lisp\hb.lsp 의 (defun c:hb ...) 알고리즘을
// 그대로 이식했다 (HHBoltTool = 접시머리렌치볼트와 사용 흐름은 완전히 동일).
//
// HHBoltTool과의 차이점(전나사 옵션 없음): hb.lsp는 hhbolt.lsp와 달리 나사부
// 길이(b) 표 자체가 없다. 대신 머리 바로 아래 무나사부 목 길이(h2, Lisp p2a-p17
// 런인 사선)가 규격표에 고정값으로 정의돼 있어, 골지름선이 항상 "목 길이만큼
// 뺀 지점부터 끝단까지"만 그려진다 — 다른 볼트류의 "전나사/부분나사" 토글과
// 대응되는 데이터 자체가 없으므로, 사용자 요청대로 그 옵션 없이 Lisp 형상을
// 그대로 이식했다(항상 h2만큼 뺀 위치부터 그림).
//
// 입력/상태머신/레이어배치 공통 로직은 AbstractBoltPlacementTool이 처리한다
// ([[project_mechparts_plan]] 참고, 4개 볼트 툴 공통화 리팩터링). 이 클래스는
// 둥근머리렌치볼트 고유의 규격(HBBoltSize)과 형상 계산만 담당한다.
//
// 길이(L) 의미: Lisp 프롬프트가 "Bolt length (@except head)"이므로, 길이는
// 머리를 제외한 몸통(어깨~끝단) 길이다. 원점(pt)은 머리 상단이므로, 끝단까지의
// 실제 거리는 (머리높이 headH + L)이 된다 — HHBoltTool(원점=머리 상단, 길이=머리
// 포함 전체길이)과는 다른 조합이니 주의.
// ─────────────────────────────────────────────────────────────────────────
class HBBoltTool : public AbstractBoltPlacementTool {
    Q_OBJECT
public:
    explicit HBBoltTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;

    // 로컬 좌표계(원점=입면도 머리 "상단" 중심, +v=볼트가 뻗어나가는 방향) 기준
    // 형상 목록 생성 — 입면도(둥근머리+육각 소켓+몸통, 모따기, 나사 골지름 표시선,
    // 중심선)와 원형 러벤도(머리 외곽원+상단평면원+육각형+중심 십자)를 모두 포함한다.
    // Lisp\hb.lsp 의 (defun c:hb ...) 알고리즘을 그대로 이식한 것 — 나사부 길이
    // 표가 없어 BoltTool/WrenchBoltTool/HHBoltTool과 달리 전나사 옵션이 없다.
    // includeCenterline=false면 입면도 중심선(p18-p19)과 평면도 중심 십자선을 아예 만들지 않는다.
    // includeSide=false면 평면도(원형 러벤도) 자체를 아예 만들지 않는다.
    static QList<BoltSegment>    buildLocalSegments(const HBBoltSize &size, double length,
                                                    bool includeCenterline = true, bool includeSide = true);
    static QList<BoltArcSpec>    buildLocalArcs(const HBBoltSize &size);
    // includeSide=false면 평면도 원(머리 외곽원+상단평면원)도 만들지 않는다.
    static QList<BoltCircleSpec> buildLocalCircles(const HBBoltSize &size, bool includeSide = true);

    // 위 로컬 형상을 기준점(origin)/각도(angleDeg)로 world 좌표에 배치해
    // 실제 LineItem/ArcItem/CircleItem 목록으로 만든다. layerName/pen은 호출부에서 적용한다.
    static QList<QGraphicsItem *> buildGeometry(const HBBoltSize &size, double length,
                                                const QPointF &origin, double angleDeg,
                                                bool includeCenterline = true, bool includeSide = true);

    // 고스트 미리보기용: 위와 동일한 형상을 QPainterPath 로 반환
    static QPainterPath buildWorldPath(const HBBoltSize &size, double length,
                                       const QPointF &origin, double angleDeg,
                                       bool includeCenterline = true, bool includeSide = true);

protected:
    QList<QGraphicsItem *> buildPlacementGeometry(double length, const QPointF &origin,
                                                  double angleDeg) const override;
    QPainterPath buildPlacementPreviewPath(double length, const QPointF &origin,
                                           double angleDeg) const override;
    QString positionPromptText() const override;
    QString toolMacroLabel() const override;

private:
    HBBoltSize     m_size;            // 선택된 호칭경 규격
};

#endif // HBBOLTTOOL_H
