#ifndef WRENCHBOLTTOOL_H
#define WRENCHBOLTTOOL_H

#include "abstractboltplacementtool.h"
#include "boltgeometry.h"
#include "mechpartstandards.h"

#include <QPointF>
#include <QList>
#include <QString>
#include <QPainterPath>

class QGraphicsItem;

// ─────────────────────────────────────────────────────────────────────────
// WrenchBoltTool — 렌치볼트(육각구멍붙이볼트, 소켓헤드캡스크류) 정면도 간략
// 기호를 배치하는 툴. Lisp\wbolt.lsp 의 (defun c:wbolt ...) 알고리즘을 그대로
// 이식했다 (BoltTool = 육각볼트와 사용 흐름은 완전히 동일).
//
// 입력/상태머신/레이어배치 공통 로직은 AbstractBoltPlacementTool이 처리한다
// (boltgeometry.h/abstractboltplacementtool.h 참고 — 4개 볼트 툴 공통화 리팩터링,
// [[project_mechparts_plan]]).이 클래스는 렌치볼트 고유의 규격(WrenchBoltSize)과
// 형상 계산(buildLocalSegments/Arcs/Circles)만 담당한다.
// ─────────────────────────────────────────────────────────────────────────
class WrenchBoltTool : public AbstractBoltPlacementTool {
    Q_OBJECT
public:
    explicit WrenchBoltTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;

    // 로컬 좌표계(원점=입면도 머리/몸통 경계 중심, +v=볼트가 뻗어나가는 방향) 기준
    // 형상 목록 생성 — 입면도(원통 머리+육각 소켓+몸통, 필렛/모따기, 나사 골지름 표시선,
    // 중심선)와 원형 러벤도(머리 외곽원+모따기원+육각형+중심 십자)를 모두 포함한다.
    // Lisp\wbolt.lsp 의 (defun c:wbolt ...) 알고리즘을 그대로 이식한 것.
    // fullThread=false(부분나사)면 골지름선이 어깨(원래 위치)가 아니라 끝단에서
    // 나사부 길이(b, mechpartstandards.h의 hexBoltThreadLength())만큼만 그려지고, 그
    // 시작점에서 60도 방향으로 외형선까지 이어지는 짧은 사선(런아웃 표시)이 추가된다.
    // (Lisp 원본은 이 구분이 없이 항상 어깨까지 전나사로만 그린다 — BoltTool과 동일하게
    //  전나사/부분나사를 선택할 수 있도록 이 부분만 새로 추가했다)
    // includeCenterline=false면 입면도 중심선(c1-c2)과 평면도 중심 십자선을 아예 만들지 않는다.
    // includeSide=false면 평면도(원형 러벤도) 자체를 아예 만들지 않는다.
    static QList<BoltSegment>    buildLocalSegments(const WrenchBoltSize &size, double length, bool fullThread = true,
                                                    bool includeCenterline = true, bool includeSide = true);
    static QList<BoltArcSpec>    buildLocalArcs(const WrenchBoltSize &size);
    // includeSide=false면 평면도 원(머리 외곽원/모따기원)도 만들지 않는다.
    static QList<BoltCircleSpec> buildLocalCircles(const WrenchBoltSize &size, bool includeSide = true);

    // 위 로컬 형상을 기준점(origin)/각도(angleDeg)로 world 좌표에 배치해
    // 실제 LineItem/ArcItem/CircleItem 목록으로 만든다. layerName/pen은 호출부에서 적용한다.
    static QList<QGraphicsItem *> buildGeometry(const WrenchBoltSize &size, double length,
                                                const QPointF &origin, double angleDeg,
                                                bool fullThread = true,
                                                bool includeCenterline = true, bool includeSide = true);

    // 고스트 미리보기용: 위와 동일한 형상을 QPainterPath 로 반환
    static QPainterPath buildWorldPath(const WrenchBoltSize &size, double length,
                                       const QPointF &origin, double angleDeg, bool fullThread = true,
                                       bool includeCenterline = true, bool includeSide = true);

protected:
    QList<QGraphicsItem *> buildPlacementGeometry(double length, const QPointF &origin,
                                                  double angleDeg) const override;
    QPainterPath buildPlacementPreviewPath(double length, const QPointF &origin,
                                           double angleDeg) const override;
    QString positionPromptText() const override;
    QString toolMacroLabel() const override;

private:
    WrenchBoltSize m_size;            // 선택된 호칭경 규격
    bool           m_fullThread;      // true면 전나사(어깨까지 골지름선), false면 부분나사(끝단에서 b만큼만)
};

#endif // WRENCHBOLTTOOL_H
