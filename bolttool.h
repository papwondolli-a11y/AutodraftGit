#ifndef BOLTTOOL_H
#define BOLTTOOL_H

#include "abstractboltplacementtool.h"
#include "boltgeometry.h"
#include "mechpartstandards.h"

#include <QPointF>
#include <QList>
#include <QString>
#include <QPainterPath>

class QGraphicsItem;

// ─────────────────────────────────────────────────────────────────────────
// BoltTool — 육각볼트(KS B 1002) 정면도 간략 기호를 배치하는 툴
//
// 입력/상태머신/레이어배치 공통 로직은 AbstractBoltPlacementTool이 처리한다
// ([[project_mechparts_plan]] 참고, 4개 볼트 툴 공통화 리팩터링). 이 클래스는
// 육각볼트 고유의 규격(HexBoltSize)과 형상 계산만 담당한다.
//
// 다른 3개 볼트 툴(WrenchBoltTool/HHBoltTool/HBBoltTool)과의 차이 2가지(공통화
// 리팩터링 때 실수로 지워지지 않게 명시적으로 남겨둠):
//   1) 원형 러벤도의 대변원을 BoltLineKind::Phantom(가상선, 2점쇄선)으로 그린다
//      — 나머지 3개 툴은 Phantom을 아예 쓰지 않는다.
//   2) buildGeometry()가 만드는 아이템은 그립 편집이 켜져 있다(gripsEnabled=true) —
//      나머지 3개 툴은 인접 도형 그립 오작동 문제 때문에 꺼둔다.
// ─────────────────────────────────────────────────────────────────────────
class BoltTool : public AbstractBoltPlacementTool {
    Q_OBJECT
public:
    explicit BoltTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;

    // 로컬 좌표계(원점=입면도 머리/몸통 경계 중심, +v=볼트가 뻗어나가는 방향) 기준
    // 형상 목록 생성 — 입면도(머리+몸통, 필렛/모따기, 나사 골지름 표시선, 중심선)와
    // 원형 러벤도(육각형, 대변원, 중심선 십자)를 모두 포함한다.
    // Lisp\BOLTNUT.LSP 의 (defun C:BOLT ...) "A"(Hex.Bolt) 옵션 알고리즘을 그대로 이식한 것.
    // fullThread=false(부분나사)면 골지름선이 어깨(p13/p16 원래 위치)가 아니라 끝단에서
    // 나사부 길이(b, mechpartstandards.h의 hexBoltThreadLength())만큼만 그려지고, 그
    // 시작점에서 60도 방향으로 외형선까지 이어지는 짧은 사선(런아웃 표시)이 추가된다.
    // includeCenterline=false면 입면도 중심선(c1-c2)과 평면도 중심 십자선을 아예 만들지 않는다.
    // includeSide=false면 평면도(원형 러벤도 — 육각형+중심십자선) 자체를 아예 만들지 않는다.
    static QList<BoltSegment>    buildLocalSegments(const HexBoltSize &size, double length, bool fullThread = true,
                                                    bool includeCenterline = true, bool includeSide = true);
    static QList<BoltArcSpec>    buildLocalArcs(const HexBoltSize &size, double length);
    // includeSide=false면 대변원(가상선)도 만들지 않는다.
    static QList<BoltCircleSpec> buildLocalCircles(const HexBoltSize &size, bool includeSide = true);

    // 위 로컬 형상을 기준점(origin)/각도(angleDeg)로 world 좌표에 배치해
    // 실제 LineItem/CircleItem 목록으로 만든다. layerName/pen은 호출부에서 적용한다.
    static QList<QGraphicsItem *> buildGeometry(const HexBoltSize &size, double length,
                                                const QPointF &origin, double angleDeg,
                                                bool fullThread = true,
                                                bool includeCenterline = true, bool includeSide = true);

    // 고스트 미리보기용: 위와 동일한 형상을 QPainterPath 로 반환
    static QPainterPath buildWorldPath(const HexBoltSize &size, double length,
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
    HexBoltSize m_size;             // 선택된 호칭경 규격
    bool        m_fullThread;       // true면 전나사(어깨까지 골지름선), false면 부분나사(끝단에서 b만큼만)
};

#endif // BOLTTOOL_H
