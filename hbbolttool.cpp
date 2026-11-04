#include "hbbolttool.h"
#include "hbboltsetupdialog.h"
#include "boltgeometry.h"
#include "iappcontext.h"
#include "tooldefaults.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

// 3-2: 도구 기본값은 ToolDefaults(도면 스코프 우선). 네임스페이스 "HBBoltTool".

HBBoltTool::HBBoltTool(QObject *parent)
    : AbstractBoltPlacementTool(parent)
{}

void HBBoltTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);

    // 마지막으로 사용한 호칭경/길이/레이어(외형선·골지름선)/2점지정여부를 다음 실행에도 유지한다.
    ToolDefaults bd("HBBoltTool", scene);
    QString savedDiameter     = bd.getString("Diameter", "M10");
    double  savedLength       = bd.getDouble("Length", 40.0);
    QString savedOutlineLayer = bd.getString("OutlineLayer", "");
    QString savedThreadLayer  = bd.getString("ThreadLayer", "");
    bool    savedTwoPointMode = bd.getBool("TwoPointMode", false);
    bool    savedCenterline   = bd.getBool("CenterlineEnabled", true);
    bool    savedSideView     = bd.getBool("SideViewEnabled", true);

    IAppContext *ctx = getContext();
    HBBoltSetupDialog dlg(ctx);
    dlg.setInitialSelection(savedDiameter, savedLength, savedOutlineLayer, savedThreadLayer,
                            savedTwoPointMode, savedCenterline, savedSideView);

    if (dlg.exec() != QDialog::Accepted) {
        emit toolFinished();
        return;
    }

    m_size             = dlg.selectedSize();
    m_length           = dlg.selectedLength();
    m_outlineLayerName = dlg.selectedOutlineLayer();
    m_threadLayerName  = dlg.selectedThreadLayer();
    m_twoPointMode     = dlg.twoPointMode();
    m_centerlineEnabled = dlg.centerlineEnabled();
    m_sideViewEnabled  = dlg.sideViewEnabled();

    bd.put("Diameter", m_size.label);
    bd.put("Length", m_length);
    bd.put("OutlineLayer", m_outlineLayerName);
    bd.put("ThreadLayer", m_threadLayerName);
    bd.put("TwoPointMode", m_twoPointMode);
    bd.put("CenterlineEnabled", m_centerlineEnabled);
    bd.put("SideViewEnabled", m_sideViewEnabled);

    finishActivation(tr("둥근머리렌치볼트 배치 (%1 x %2)").arg(m_size.label).arg(m_length));
}

QString HBBoltTool::positionPromptText() const {
    return tr(" 둥근머리렌치볼트 삽입 기준점(머리 상단 중심, Lisp의 \"Pick upper center\") 지정: ");
}

QString HBBoltTool::toolMacroLabel() const {
    return tr("둥근머리렌치볼트 배치");
}

QList<QGraphicsItem *> HBBoltTool::buildPlacementGeometry(double length, const QPointF &origin,
                                                           double angleDeg) const {
    return buildGeometry(m_size, length, origin, angleDeg, m_centerlineEnabled, m_sideViewEnabled);
}

QPainterPath HBBoltTool::buildPlacementPreviewPath(double length, const QPointF &origin,
                                                    double angleDeg) const {
    return buildWorldPath(m_size, length, origin, angleDeg, m_centerlineEnabled, m_sideViewEnabled);
}

// ─────────────────────────────────────────────────────────────────────────
// 아래 buildLocalSegments/buildLocalArcs/buildLocalCircles 는
// Lisp\hb.lsp 의 (defun c:hb ...) 알고리즘을 그대로 이식한 것이다. 로컬
// 좌표계는 Lisp의 pt(="Pick upper center", 즉 둥근머리 상단면 중심)를 원점으로
// 두고, Lisp의 +Y(머리쪽)를 내 -v로, Lisp의 -Y(끝단쪽)를 내 +v로 대응시킨다
// (Lisp_Y → v = -Lisp_Y, X는 그대로 u). BoltTool/WrenchBoltTool/HHBoltTool과 동일한 규약.
// fromLisp/circumcenter3는 boltgeometry.h의 공유 함수를 그대로 쓴다.
//
// 길이(length)는 Lisp의 bl과 동일 — "Bolt length (@except head)", 즉 머리를
// 제외한 어깨~끝단 길이다. 원점은 머리 상단이므로 끝단까지의 총 거리는 headH+bl.
// ─────────────────────────────────────────────────────────────────────────

QList<BoltSegment> HBBoltTool::buildLocalSegments(const HBBoltSize &size, double length,
                                                   bool includeCenterline, bool includeSide) {
    QList<BoltSegment> segs;

    const double an2 = size.d;            // 호칭경(나사 바깥지름) — Lisp an2
    const double PH  = size.pitch;        // Lisp ph
    const double K   = size.tipChamferK;  // 끝단 모따기 높이 — Lisp k
    const double A   = size.headD;        // 둥근머리 지름 — Lisp a
    const double H   = size.headH;        // 머리 높이 — Lisp h
    const double H1  = size.socketDepth;  // 육각 소켓 깊이 — Lisp h1
    const double G   = size.socketWidth;  // 육각 소켓 대변거리 — Lisp g
    const double H2  = size.neckLen;      // 머리 바로 아래 무나사부 목 길이 — Lisp h2
    const double bl  = length;            // 몸통(머리 제외) 길이 — Lisp bl

    const double gd = an2 - 1.082532 * PH;                      // 나사 골지름(minor diameter) — Lisp gd
    const double dg = G / qCos(qDegreesToRadians(30.0));        // 소켓 육각 대각지름(across corners) — Lisp dg
    const double h3 = (A - dg - 0.6) / 2.0;                     // 머리 상단 모서리 필렛 반지름 — Lisp h3

    // ── Lisp 점들 (변수명 그대로 유지) ─────────────────────────
    QPointF p5  = fromLisp(-A/2.0, -H);
    QPointF p6  = fromLisp( A/2.0, -H);
    QPointF p5a = fromLisp(-(A/2.0 - h3), 0);
    QPointF p5b = fromLisp( (A/2.0 - h3), 0);
    QPointF p5c = fromLisp(-A/2.0, -h3);
    QPointF p6c = fromLisp( A/2.0, -h3); // p5c의 미러(우) — Lisp에서는 별도 이름 없이 mirror로만 생성됨
    QPointF p7  = fromLisp( an2/2.0, -H);
    QPointF p9  = fromLisp(-an2/2.0, -H);

    // ── 머리(둥근머리) 외형선 ────────────────────────────────
    segs.append({ p5a, p5b, BoltLineKind::Object }); // 머리 상단 평면(모따기로 남는 폭)
    segs.append({ p5,  p6,  BoltLineKind::Object }); // 머리/몸통 경계 어깨선(머리 전체 폭 A)
    segs.append({ p5,  p5c, BoltLineKind::Object }); // 머리 상단 모서리 아래 수직단(좌)
    segs.append({ p6,  p6c, BoltLineKind::Object }); // 머리 상단 모서리 아래 수직단(우)
    // 머리 상단 모서리 필렛(반지름 h3의 사분원, p5a↔p5c / p5b↔p6c)은 buildLocalArcs()에서 그린다.

    // ── 몸통/끝단 외형선 ─────────────────────────────────────
    QPointF p11 = fromLisp( gd/2.0, -(H + bl));
    QPointF p12 = fromLisp(-gd/2.0, -(H + bl));
    QPointF p14 = fromLisp( an2/2.0, -(H + bl - K));
    QPointF p15 = fromLisp(-an2/2.0, -(H + bl - K));

    segs.append({ p9, p15, BoltLineKind::Object }); // 몸통 좌측벽(어깨~끝단모따기 시작, 목 구간도 이음새 없이 연속)
    segs.append({ p7, p14, BoltLineKind::Object }); // 몸통 우측벽
    segs.append({ p15, p14, BoltLineKind::Object, true }); // 끝단 모따기 시작선 — 골지름선 레이어(다른 볼트류와 동일 처리)
    segs.append({ p15, p12, BoltLineKind::Object }); // 끝단 모따기(좌)
    segs.append({ p14, p11, BoltLineKind::Object }); // 끝단 모따기(우)
    segs.append({ p12, p11, BoltLineKind::Object }); // 끝단 평면

    // ── 무나사부 목(H2) 경계 + 나사 골지름 표시선 ───────────────
    // hb.lsp는 나사부 길이 표가 없어 BoltTool/WrenchBoltTool/HHBoltTool처럼 전나사를 계산할
    // 방법이 없다 — 대신 규격표에 고정된 목 길이(H2)만큼 어깨 아래는 항상 무나사부로 남기고,
    // 그 아래(끝단까지)만 골지름선을 그린다(Lisp 원본 그대로, 토글 없음).
    QPointF p2 = fromLisp(-an2/2.0, -(H + H2));
    QPointF p4 = fromLisp( an2/2.0, -(H + H2));
    QPointF p2a = fromLisp(-an2/2.0, -(H + H2/2.0));
    QPointF p4a = fromLisp( an2/2.0, -(H + H2/2.0)); // p2a의 미러(우)
    QPointF p16 = fromLisp( gd/2.0, -(H + H2));
    QPointF p17 = fromLisp(-gd/2.0, -(H + H2));

    segs.append({ p2, p4, BoltLineKind::Object }); // 목 하단 경계 가로선(무나사부↔나사부 경계, 전체 an2 폭)
    segs.append({ p17, p12, BoltLineKind::ThreadIndicator, true }); // 골지름선(좌) — 목 하단부터 끝단까지
    segs.append({ p16, p11, BoltLineKind::ThreadIndicator, true }); // 골지름선(우)
    segs.append({ p2a, p17, BoltLineKind::ThreadIndicator, true }); // 나사 시작 런인 사선(좌) — 목 중간에서 골지름 시작점으로
    segs.append({ p4a, p16, BoltLineKind::ThreadIndicator, true }); // 런인 사선(우)

    // ── 육각 소켓(렌치 구멍) 입구 표시선 — 머리 상단면에서 소켓 깊이(H1)만큼 ──
    // 렌치홈 전체(투영선+바닥 지그재그+드릴 자국선+아래 buildLocalArcs의 얕은 3점호)는
    // 골지름선과 같은 레이어에 넣는다(HHBoltTool/WrenchBoltTool과 동일한 처리).
    QPointF p22 = fromLisp( dg/2.0, 0);
    QPointF p23 = fromLisp( dg/2.0, -H1);
    QPointF p20 = fromLisp( dg/4.0, 0);
    QPointF p21 = fromLisp( dg/4.0, -H1);
    segs.append({ p22, p23, BoltLineKind::Object, true }); // 소켓 외곽 모서리 투영선(우)
    segs.append({ p20, p21, BoltLineKind::Object, true }); // 소켓 내측 모서리 투영선(우)
    segs.append({ fromLisp(-dg/2.0, 0), fromLisp(-dg/2.0, -H1), BoltLineKind::Object, true }); // 소켓 외곽 모서리 투영선(좌)
    segs.append({ fromLisp(-dg/4.0, 0), fromLisp(-dg/4.0, -H1), BoltLineKind::Object, true }); // 소켓 내측 모서리 투영선(좌)

    // 소켓 바닥이 드릴 끝점(pt1, 어깨 바로 위)을 향해 좁아드는 모습을 나타내는 대각선
    QPointF pt1 = fromLisp(0, -H + 0.2);
    segs.append({ pt1, fromLisp(-dg/2.0, -H1), BoltLineKind::Object, true });
    segs.append({ pt1, fromLisp( dg/2.0, -H1), BoltLineKind::Object, true });

    // ── 입면도 중심선 (머리 위/끝단 아래로 1.5mm씩 더 나오게 — Lisp p18-p19) ──
    if (includeCenterline) {
        QPointF c1 = fromLisp(0, 1.5);
        QPointF c2 = fromLisp(0, -(H + bl + 1.5));
        segs.append({ c1, c2, BoltLineKind::Centerline });
    }

    // ── 원형 러벤도 (평면도 — 육각형 + 중심 십자선) ────────────
    if (includeSide) {
        const double gap = 5.0;
        QPointF s1 = fromLisp(0, 1.5 + A/2.0 + gap); // Lisp p25: p18 위로 (A/2+5) 만큼 더

        // 육각형(소켓) 꼭짓점 6개: 대각지름 dg(circumradius=dg/2), 대변거리 G
        const double R = dg / 2.0;
        const double apo = G / 2.0;
        QPointF hv0(s1.x() + R,       s1.y());
        QPointF hv1(s1.x() + R*0.5,   s1.y() + apo);
        QPointF hv2(s1.x() - R*0.5,   s1.y() + apo);
        QPointF hv3(s1.x() - R,       s1.y());
        QPointF hv4(s1.x() - R*0.5,   s1.y() - apo);
        QPointF hv5(s1.x() + R*0.5,   s1.y() - apo);
        segs.append({ hv0, hv1, BoltLineKind::Object });
        segs.append({ hv1, hv2, BoltLineKind::Object });
        segs.append({ hv2, hv3, BoltLineKind::Object });
        segs.append({ hv3, hv4, BoltLineKind::Object });
        segs.append({ hv4, hv5, BoltLineKind::Object });
        segs.append({ hv5, hv0, BoltLineKind::Object });

        // 중심 십자선(Lisp p26/p27 + 90도 회전 복사 상당) — 길이는 머리 반지름(A/2)+1.5 기준
        if (includeCenterline) {
            double crossExt = A/2.0 + 1.5;
            segs.append({ QPointF(s1.x()-crossExt, s1.y()), QPointF(s1.x()+crossExt, s1.y()), BoltLineKind::Centerline });
            segs.append({ QPointF(s1.x(), s1.y()-crossExt), QPointF(s1.x(), s1.y()+crossExt), BoltLineKind::Centerline });
        }
    }

    return segs;
}

QList<BoltArcSpec> HBBoltTool::buildLocalArcs(const HBBoltSize &size) {
    QList<BoltArcSpec> arcs;

    const double A  = size.headD;
    const double G  = size.socketWidth;
    const double H1 = size.socketDepth;

    const double dg = G / qCos(qDegreesToRadians(30.0));
    const double h3 = (A - dg - 0.6) / 2.0;

    // ── 머리 상단 모서리 필렛(좌/우, 반지름 h3의 사분원) ──
    // Lisp: (command "arc" p5a "e" p5c "d" p1) — 시작점 p5a, 끝점 p5c, 시작 접선방향을
    // p1(같은 y=0, 즉 수평 방향)으로 지정하는 아크. 시작 접선이 수평인 경우 중심은 항상
    // (시작점.x, 끝점.y)로 단순화된다(유도: |중심-p5a|=|중심-p5c|를 풀면 중심.y=p5c.y).
    {
        QPointF p5a = fromLisp(-(A/2.0 - h3), 0);
        QPointF p5c = fromLisp(-A/2.0, -h3);
        QPointF centerL(p5a.x(), p5c.y());
        arcs.append({ centerL, p5a, p5c, BoltLineKind::Object, false });

        QPointF p5b = fromLisp( (A/2.0 - h3), 0);
        QPointF p6c = fromLisp( A/2.0, -h3);
        QPointF centerR(p5b.x(), p6c.y());
        arcs.append({ centerR, p5b, p6c, BoltLineKind::Object, false });
    }

    // ── 소켓 입구의 얕은 곡선(3점호) — Lisp e13/e13a/e18 상당 ──────────
    QPointF p23 = fromLisp( dg/2.0, -H1);
    QPointF p21 = fromLisp( dg/4.0, -H1);
    QPointF p20L = fromLisp(-dg/4.0, -H1);
    QPointF p23L = fromLisp(-dg/2.0, -H1);
    const double dipDepth = 0.6; // Lisp 원본 고정 상수(hhbolt.lsp의 per-size "c"와 달리 hb.lsp는 상수 사용)

    // 좌측 얕은 호: p23L-p20L 사이
    double midXL = -(dg/2.0 + dg/4.0) / 2.0;
    QPointF dipL = fromLisp(midXL, -H1 - dipDepth);
    QPointF centerDipL;
    if (circumcenter3(p23L, dipL, p20L, centerDipL)) {
        arcs.append({ centerDipL, p23L, p20L, BoltLineKind::Object, true }); // 렌치홈 일부 — 골지름선 레이어
    }

    // 중앙 얕은 호: p20L-p21 사이 — 좌우대칭이라 별도 미러가 필요없다
    QPointF dipC = fromLisp(0, -H1 - dipDepth);
    QPointF centerDipC;
    if (circumcenter3(p20L, dipC, p21, centerDipC)) {
        arcs.append({ centerDipC, p20L, p21, BoltLineKind::Object, true });
    }

    // 우측 얕은 호: 좌측 호를 x=0 기준으로 미러
    double midXR = -midXL;
    QPointF dipR = fromLisp(midXR, -H1 - dipDepth);
    QPointF centerDipR;
    if (circumcenter3(p21, dipR, p23, centerDipR)) {
        arcs.append({ centerDipR, p21, p23, BoltLineKind::Object, true });
    }

    return arcs;
}

// 원형 러벤도에 그릴 원 2개. Lisp에서는 "머리 지름(A)" 원과 "상단 평면 지름
// (distance p5a p5b = A-2*h3)" 원을 그린다(WrenchBoltTool과 동일한 2원 패턴).
QList<BoltCircleSpec> HBBoltTool::buildLocalCircles(const HBBoltSize &size, bool includeSide) {
    QList<BoltCircleSpec> circles;
    if (!includeSide) return circles;

    const double A = size.headD;
    const double G = size.socketWidth;
    const double dg = G / qCos(qDegreesToRadians(30.0));
    const double h3 = (A - dg - 0.6) / 2.0;
    const double gap = 5.0;
    QPointF s1 = fromLisp(0, 1.5 + A/2.0 + gap);

    circles.append({ s1, A/2.0 });              // 머리 외곽원
    circles.append({ s1, (A - 2.0*h3) / 2.0 }); // 상단 평면 지름원
    return circles;
}

QPainterPath HBBoltTool::buildWorldPath(const HBBoltSize &size, double length,
                                        const QPointF &origin, double angleDeg,
                                        bool includeCenterline, bool includeSide) {
    return buildBoltWorldPath(buildLocalSegments(size, length, includeCenterline, includeSide),
                              buildLocalArcs(size),
                              buildLocalCircles(size, includeSide),
                              origin, angleDeg);
}

QList<QGraphicsItem *> HBBoltTool::buildGeometry(const HBBoltSize &size, double length,
                                                 const QPointF &origin, double angleDeg,
                                                 bool includeCenterline, bool includeSide) {
    return buildBoltGraphicsItems(buildLocalSegments(size, length, includeCenterline, includeSide),
                                  buildLocalArcs(size),
                                  buildLocalCircles(size, includeSide),
                                  origin, angleDeg, /*gripsEnabled=*/false);
}
