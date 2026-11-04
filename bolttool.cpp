#include "bolttool.h"
#include "boltsetupdialog.h"
#include "boltgeometry.h"
#include "iappcontext.h"
#include "tooldefaults.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

// 3-2: 도구 기본값은 ToolDefaults 를 통해 도면 스코프 우선으로 조회/기록한다.
//   네임스페이스 "BoltTool" = 기존 QSettings 그룹명 그대로.

BoltTool::BoltTool(QObject *parent)
    : AbstractBoltPlacementTool(parent), m_fullThread(true)
{}

void BoltTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);

    // 마지막으로 사용한 호칭경/길이/레이어(외형선·골지름선)/2점지정여부/전나사여부를 다음 실행에도 유지한다.
    ToolDefaults bd("BoltTool", scene);
    QString savedDiameter     = bd.getString("Diameter", "M10");
    double  savedLength       = bd.getDouble("Length", 40.0);
    QString savedOutlineLayer = bd.getString("OutlineLayer", "");
    QString savedThreadLayer  = bd.getString("ThreadLayer", "");
    bool    savedTwoPointMode = bd.getBool("TwoPointMode", false);
    bool    savedFullThread   = bd.getBool("FullThread", true);
    bool    savedCenterline   = bd.getBool("CenterlineEnabled", true);
    bool    savedSideView     = bd.getBool("SideViewEnabled", true);

    IAppContext *ctx = getContext();
    BoltSetupDialog dlg(ctx);
    dlg.setInitialSelection(savedDiameter, savedLength, savedOutlineLayer, savedThreadLayer,
                            savedTwoPointMode, savedFullThread, savedCenterline,
                            savedSideView);

    if (dlg.exec() != QDialog::Accepted) {
        emit toolFinished();
        return;
    }

    m_size             = dlg.selectedSize();
    m_length           = dlg.selectedLength();
    m_outlineLayerName = dlg.selectedOutlineLayer();
    m_threadLayerName  = dlg.selectedThreadLayer();
    m_twoPointMode     = dlg.twoPointMode();
    m_fullThread       = dlg.fullThread();
    m_centerlineEnabled = dlg.centerlineEnabled();
    m_sideViewEnabled  = dlg.sideViewEnabled();

    bd.put("Diameter", m_size.label);
    bd.put("Length", m_length);
    bd.put("OutlineLayer", m_outlineLayerName);
    bd.put("ThreadLayer", m_threadLayerName);
    bd.put("TwoPointMode", m_twoPointMode);
    bd.put("FullThread", m_fullThread);
    bd.put("CenterlineEnabled", m_centerlineEnabled);
    bd.put("SideViewEnabled", m_sideViewEnabled);

    finishActivation(tr("볼트 배치 (%1 x %2)").arg(m_size.label).arg(m_length));
}

QString BoltTool::positionPromptText() const {
    return tr(" 볼트 삽입 기준점(머리/몸통 경계선 중앙) 지정: ");
}

QString BoltTool::toolMacroLabel() const {
    return tr("볼트 배치");
}

QList<QGraphicsItem *> BoltTool::buildPlacementGeometry(double length, const QPointF &origin,
                                                         double angleDeg) const {
    return buildGeometry(m_size, length, origin, angleDeg, m_fullThread, m_centerlineEnabled, m_sideViewEnabled);
}

QPainterPath BoltTool::buildPlacementPreviewPath(double length, const QPointF &origin,
                                                  double angleDeg) const {
    return buildWorldPath(m_size, length, origin, angleDeg, m_fullThread, m_centerlineEnabled, m_sideViewEnabled);
}

// ─────────────────────────────────────────────────────────────────────────
// 아래 buildLocalSegments/buildLocalArcs/buildLocalCircles 는
// Lisp\BOLTNUT.LSP 의 (defun C:BOLT ...) "A"(Hex.Bolt) 옵션 알고리즘을 그대로
// 이식한 것이다. 로컬 좌표계는 Lisp의 pt(="Pick head lower center", 즉
// 머리/몸통 경계)를 원점으로 두고, Lisp의 +Y(머리쪽)를 내 -v로, Lisp의 -Y(끝단쪽)를
// 내 +v로 대응시킨다 (Lisp_Y → v = -Lisp_Y, X는 그대로 u).
// fromLisp/circumcenter3는 boltgeometry.h의 공유 함수를 그대로 쓴다.
// ─────────────────────────────────────────────────────────────────────────

QList<BoltSegment> BoltTool::buildLocalSegments(const HexBoltSize &size, double length, bool fullThread,
                                                bool includeCenterline, bool includeSide) {
    QList<BoltSegment> segs;

    const double an2 = size.d;           // 호칭경(나사 바깥지름) — Lisp an2
    const double HH  = size.k;           // 머리 높이 — Lisp h
    const double s   = size.s;           // 대변거리 — Lisp b
    const double e   = size.e();         // 대각거리(across corners) — Lisp dg
    const double dch = size.chamferD;    // 모따기 후 남는 원 지름 — Lisp d
    const double L   = length;           // 몸통(나사부) 길이 — Lisp bl
    const double pitch = size.pitch;

    const double hc = (e - dch) / 2.0 / qSin(qDegreesToRadians(60.0)); // 머리 모따기 길이
    const double h1 = hc * qSin(qDegreesToRadians(30.0));              // 돔 아크 높이
    const double q  = s / qSqrt(3.0);                                  // 육각 파셋선 폭 (2*tan30*(s/2))

    const double gd = an2 - 1.082532 * pitch; // 나사 골지름(minor diameter)
    const double dp = (an2 - gd) / 2.0;       // 골 깊이

    // ── Lisp 점들 (변수명 그대로 유지) ─────────────────────────
    QPointF p1a = fromLisp( an2/2.0, 0);
    QPointF p2  = fromLisp( e/2.0,   0);
    QPointF p4  = fromLisp( dch/2.0, HH);
    QPointF p3b = fromLisp( e/2.0,   HH - h1);
    QPointF p7  = fromLisp(-dch/2.0, HH);
    QPointF p8b = fromLisp(-e/2.0,   HH - h1);
    QPointF p9  = fromLisp(-e/2.0,   0);
    QPointF p10a= fromLisp(-an2/2.0, 0);
    QPointF p1  = fromLisp( q/2.0,   0);
    QPointF p5  = fromLisp( q/2.0,   HH - h1);
    QPointF p6  = fromLisp(-q/2.0,   HH - h1);
    QPointF p10 = fromLisp(-q/2.0,   0);

    // p17-p18 ~ p14-p15 간격(끝단 모따기 크기)은 몸통선(an2)-골지선(gd) 간격(dp)과
    // 동일하게 맞춘다 (원래 Lisp의 kk 상수 대신 dp 사용).
    QPointF p14 = fromLisp(-an2/2.0 + dp, -L);
    QPointF p15 = fromLisp( an2/2.0 - dp, -L);
    QPointF p17 = fromLisp(-an2/2.0,      -L + dp);
    QPointF p18 = fromLisp( an2/2.0,      -L + dp);

    // 골지름선(p13-p14, p16-p15) 길이 — 전나사면 어깨(shoulder)부터 끝단(p14/p15)까지 전체(L),
    // 부분나사면 끝단에서 나사부 길이 b(hexBoltThreadLength)만큼만 그린다. p14/p15는 X좌표가
    // 이미 ∓gd/2(=an2/2-dp)라서 p13/p16과 같은 골지름선 위에 있으므로 그대로 잇는다.
    double bLen = hexBoltThreadLength(an2, L);           // 나사부 길이 b
    double threadStartLen = fullThread ? L : std::min(bLen, L);
    double y13 = -(L - threadStartLen);  // p13/p16의 v(Lisp Y) 위치 — 전나사면 0(어깨)

    QPointF p13 = fromLisp(-gd/2.0, y13);
    QPointF p16 = fromLisp( gd/2.0, y13);

    // ── 머리 외형선 (e1~e5, e10, e11 상당) ─────────────────────
    segs.append({ p2,  p3b, BoltLineKind::Object }); // 머리 바깥벽(우)
    segs.append({ p3b, p4,  BoltLineKind::Object }); // 머리 모따기(우)
    segs.append({ p7,  p4,  BoltLineKind::Object }); // 머리 상단 평면
    segs.append({ p8b, p7,  BoltLineKind::Object }); // 머리 모따기(좌)
    segs.append({ p9,  p8b, BoltLineKind::Object }); // 머리 바깥벽(좌)
    segs.append({ p1,  p5,  BoltLineKind::Object }); // 육각 파셋선(우)
    segs.append({ p6,  p10, BoltLineKind::Object }); // 육각 파셋선(좌)
    segs.append({ p9,  p2,  BoltLineKind::Object }); // 머리/몸통 경계 어깨선 (머리 전체 폭)

    // ── 몸통/끝단 외형선 (e12~e15, e19, e20 상당) ───────────────
    segs.append({ p10a, p17, BoltLineKind::Object }); // 몸통 좌측벽
    segs.append({ p18,  p1a, BoltLineKind::Object }); // 몸통 우측벽
    segs.append({ p17,  p18, BoltLineKind::Object, true }); // 끝단 모따기 시작선 — 골지름선 레이어
    segs.append({ p18,  p15, BoltLineKind::Object }); // 끝단 모따기(우)
    segs.append({ p17,  p14, BoltLineKind::Object }); // 끝단 모따기(좌)
    segs.append({ p14,  p15, BoltLineKind::Object }); // 끝단 평면

    // ── 나사 골지름 표시선 (e16, e17 상당 — Lisp에서도 가는 실선, 골지름선 레이어) ──
    if (threadStartLen > 1e-6) {
        segs.append({ p13, p14, BoltLineKind::ThreadIndicator, true });
        segs.append({ p16, p15, BoltLineKind::ThreadIndicator, true });

        // 부분나사(전나사 해제)이면서, 볼트 길이(L)가 나사부 길이(b)보다 2.34 이상
        // 클 때만(=어깨~나사 시작 구간이 충분히 있을 때만) 런아웃 사선/연결선을 그린다.
        // 그렇지 않으면(L이 b에 가까우면) 그릴 공간이 부족해 생략한다.
        //  - p13/p16에서 외형선(몸통 벽)까지, p13-p13c(직각/수평)선과 60도를 이루는
        //    방향(머리쪽으로 기울게)으로 이어지는 짧은 런아웃 사선(p13d/p16d, 골지름선 레이어)
        //  - p13/p16에서 "직각(수평, 같은 v)"으로 몸통 벽과 만나는 점(p13c/p16c)을
        //    서로 잇는, 몸통을 가로지르는 외형선 — p9-p2/p17-p18과 같은 패턴
        if (!fullThread && (L - bLen) >= 2.34) {
            // p13-p13c(수평선) 기준 60도이므로, v방향 이동량 = dp*tan(60°) (수평 기준 각도이기 때문에
            // v축 기준으로는 30도가 되어, dp/tan(60°)가 아니라 dp*tan(60°)를 써야 한다)
            double runoutDv = dp * qTan(qDegreesToRadians(60.0)); // v방향(길이축) 이동량 — +면 머리쪽
            QPointF p13d = fromLisp(-an2/2.0, y13 + runoutDv); // 런아웃 사선이 몸통 벽과 만나는 점
            QPointF p16d = fromLisp( an2/2.0, y13 + runoutDv);
            segs.append({ p13, p13d, BoltLineKind::ThreadIndicator, true });
            segs.append({ p16, p16d, BoltLineKind::ThreadIndicator, true });

            QPointF p13c = fromLisp(-an2/2.0, y13); // p13에서 직각(수평)으로 몸통 벽과 만나는 점
            QPointF p16c = fromLisp( an2/2.0, y13);
            segs.append({ p13c, p16c, BoltLineKind::Object, false }); // 몸통을 가로지르는 외형선
        }
    }

    // ── 중심선 (c1-c2 상당, 머리 위/끝단 아래로 1.5mm씩 더 나오게) ──
    if (includeCenterline) {
        QPointF c1 = fromLisp(0, HH + 1.5);
        QPointF c2 = fromLisp(0, -(L + 1.5));
        segs.append({ c1, c2, BoltLineKind::Centerline });
    }

    // ── 원형 러벤도 (평면도 — 육각형 + 중심 십자선) ────────────
    // "측면" 체크박스(includeSide)로 평면도 전체를 켜고 끌 수 있다.
    if (includeSide) {
        // 중심(s1)은 머리 위쪽으로 (대각거리 절반 + 8mm) 떨어진 자리 (Lisp s1 상당)
        const double apo = s / 2.0;
        const double gap = 8.0;
        QPointF s1 = fromLisp(0, HH + e/2.0 + gap);

        // 육각형 꼭짓점 6개: 0°,60°,...,300° 배치 → 위/아래 평평(대변폭 s), 좌/우 뾰족(대각폭 e)
        const double R = e / 2.0;
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

        // 중심 십자선 (Lisp c3/c4 + 90도 회전 복사 상당)
        if (includeCenterline) {
            double crossExt = R + 1.5;
            segs.append({ QPointF(s1.x()-crossExt, s1.y()), QPointF(s1.x()+crossExt, s1.y()), BoltLineKind::Centerline });
            segs.append({ QPointF(s1.x(), s1.y()-crossExt), QPointF(s1.x(), s1.y()+crossExt), BoltLineKind::Centerline });
        }
    }

    return segs;
}

QList<BoltArcSpec> BoltTool::buildLocalArcs(const HexBoltSize &size, double /*length*/) {
    QList<BoltArcSpec> arcs;

    const double HH  = size.k;
    const double s   = size.s;
    const double e   = size.e();
    const double dch = size.chamferD;

    const double hc = (e - dch) / 2.0 / qSin(qDegreesToRadians(60.0));
    const double h1 = hc * qSin(qDegreesToRadians(30.0));
    const double q  = s / qSqrt(3.0);

    QPointF p4  = fromLisp( dch/2.0, HH);
 //   QPointF p7  = fromLisp(-dch/2.0, HH);
    QPointF p3b = fromLisp( e/2.0,   HH - h1);
    QPointF p5  = fromLisp( q/2.0,   HH - h1);
    QPointF p6  = fromLisp(-q/2.0,   HH - h1);
    QPointF p8b = fromLisp(-e/2.0,   HH - h1);
    QPointF p19 = fromLisp( 0,       HH);

    // ── 머리 상단 돔 아크 (p5-p19-p6 3점을 지나는 원, 좌우 대칭이라 중심은 u=0 위) ──
    {
        double halfQ = q / 2.0;
        double y5 = p5.y();
        double y19 = p19.y();
        double cy = (y19*y19 - y5*y5 - halfQ*halfQ) / (2.0*(y19 - y5));
        arcs.append({ QPointF(0, cy), p5, p6, BoltLineKind::Object });
    }

    // ── 필렛 아크 (육각 파셋선 끝과 머리 바깥벽/모따기 코너를 잇는 작은 호) ──
    // 사용자 지정 방식: p5-p3b(또는 p6-p8b) 중간점에서 수직으로 올려 머리 상단
    // 평면선(p7-p4, y=HH)과 만나는 점을 3번째 점(p5a/p6a)으로 삼아 3점호를 만든다.
    {
        double topY = p4.y(); // == p7.y() == HH

        QPointF p5a((p5.x() + p3b.x()) / 2.0, topY);
        QPointF centerR;
        if (circumcenter3(p5, p5a, p3b, centerR)) {
            arcs.append({ centerR, p5, p3b, BoltLineKind::Object });
        }

        QPointF p6a((p6.x() + p8b.x()) / 2.0, topY);
        QPointF centerL;
        if (circumcenter3(p6, p6a, p8b, centerL)) {
            arcs.append({ centerL, p6, p8b, BoltLineKind::Object });
        }
    }

    return arcs;
}

// 원형 러벤도에 그릴 대변원(가상선). Lisp에서는 "모따기 후 남는 원(chamferD)"을
// 그리므로, 대변거리(s)에 딱 접하는 원이 아니라 그보다 살짝 작은 이 원을 사용한다.
QList<BoltCircleSpec> BoltTool::buildLocalCircles(const HexBoltSize &size, bool includeSide) {
    QList<BoltCircleSpec> circles;
    if (!includeSide) return circles;
    const double HH = size.k;
    const double e  = size.e();
    const double gap = 8.0;
    QPointF s1 = fromLisp(0, HH + e/2.0 + gap);
    circles.append({ s1, size.chamferD / 2.0, BoltLineKind::Phantom });
    return circles;
}

QPainterPath BoltTool::buildWorldPath(const HexBoltSize &size, double length,
                                      const QPointF &origin, double angleDeg, bool fullThread,
                                      bool includeCenterline, bool includeSide) {
    return buildBoltWorldPath(buildLocalSegments(size, length, fullThread, includeCenterline, includeSide),
                              buildLocalArcs(size, length),
                              buildLocalCircles(size, includeSide),
                              origin, angleDeg);
}

QList<QGraphicsItem *> BoltTool::buildGeometry(const HexBoltSize &size, double length,
                                               const QPointF &origin, double angleDeg,
                                               bool fullThread,
                                               bool includeCenterline, bool includeSide) {
    // BoltTool은 나머지 3개 볼트 툴과 달리 그립 편집을 켜둔 채로 유지한다(gripsEnabled=true) —
    // 기존 동작 그대로.
    return buildBoltGraphicsItems(buildLocalSegments(size, length, fullThread, includeCenterline, includeSide),
                                  buildLocalArcs(size, length),
                                  buildLocalCircles(size, includeSide),
                                  origin, angleDeg, /*gripsEnabled=*/true);
}
