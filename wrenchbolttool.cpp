#include "wrenchbolttool.h"
#include "wrenchboltsetupdialog.h"
#include "boltgeometry.h"
#include "iappcontext.h"
#include "tooldefaults.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

// 3-2: 도구 기본값은 ToolDefaults(도면 스코프 우선). 네임스페이스 "WrenchBoltTool".

WrenchBoltTool::WrenchBoltTool(QObject *parent)
    : AbstractBoltPlacementTool(parent), m_fullThread(true)
{}

void WrenchBoltTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);

    // 마지막으로 사용한 호칭경/길이/레이어(외형선·골지름선)/2점지정여부/전나사여부를 다음 실행에도 유지한다.
    ToolDefaults bd("WrenchBoltTool", scene);
    QString savedDiameter     = bd.getString("Diameter", "M10");
    double  savedLength       = bd.getDouble("Length", 40.0);
    QString savedOutlineLayer = bd.getString("OutlineLayer", "");
    QString savedThreadLayer  = bd.getString("ThreadLayer", "");
    bool    savedTwoPointMode = bd.getBool("TwoPointMode", false);
    bool    savedFullThread   = bd.getBool("FullThread", true);
    bool    savedCenterline   = bd.getBool("CenterlineEnabled", true);
    bool    savedSideView     = bd.getBool("SideViewEnabled", true);

    IAppContext *ctx = getContext();
    WrenchBoltSetupDialog dlg(ctx);
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

    finishActivation(tr("렌치볼트 배치 (%1 x %2)").arg(m_size.label).arg(m_length));
}

QString WrenchBoltTool::positionPromptText() const {
    return tr(" 렌치볼트 삽입 기준점(머리/몸통 경계, Lisp의 \"head lower center\") 지정: ");
}

QString WrenchBoltTool::toolMacroLabel() const {
    return tr("렌치볼트 배치");
}

QList<QGraphicsItem *> WrenchBoltTool::buildPlacementGeometry(double length, const QPointF &origin,
                                                               double angleDeg) const {
    return buildGeometry(m_size, length, origin, angleDeg, m_fullThread, m_centerlineEnabled, m_sideViewEnabled);
}

QPainterPath WrenchBoltTool::buildPlacementPreviewPath(double length, const QPointF &origin,
                                                        double angleDeg) const {
    return buildWorldPath(m_size, length, origin, angleDeg, m_fullThread, m_centerlineEnabled, m_sideViewEnabled);
}

// ─────────────────────────────────────────────────────────────────────────
// 아래 buildLocalSegments/buildLocalArcs/buildLocalCircles 는
// Lisp\wbolt.lsp 의 (defun c:wbolt ...) 알고리즘을 그대로 이식한 것이다. 로컬
// 좌표계는 Lisp의 pt(="Pick head lower center", 즉 머리/몸통 경계)를 원점으로
// 두고, Lisp의 +Y(머리쪽)를 내 -v로, Lisp의 -Y(끝단쪽)를 내 +v로 대응시킨다
// (Lisp_Y → v = -Lisp_Y, X는 그대로 u). BoltTool과 동일한 규약.
// fromLisp/circumcenter3는 boltgeometry.h의 공유 함수를 그대로 쓴다.
// ─────────────────────────────────────────────────────────────────────────

QList<BoltSegment> WrenchBoltTool::buildLocalSegments(const WrenchBoltSize &size, double length,
                                                       bool fullThread, bool includeCenterline,
                                                       bool includeSide) {
    QList<BoltSegment> segs;

    const double an2 = size.d;           // 호칭경(나사 바깥지름) — Lisp an2
    const double D   = size.headD;       // 머리(원통) 바깥지름 — Lisp d
    const double H   = size.headH;       // 머리 높이 — Lisp h
    const double E   = size.chamferE;    // 머리 상단/소켓 입구 모따기 상수 — Lisp e
    const double B   = size.socketWidth; // 육각 소켓 대변거리 — Lisp b
    const double M   = size.socketDepth; // 육각 소켓 깊이 — Lisp m
    const double R1  = size.filletR;     // 머리/몸통 경계 필렛 반지름 — Lisp r1
    const double PH  = size.pitch;
    const double L   = length;           // 몸통(나사부) 길이 — Lisp bl

    const double gd = an2 - 1.082532 * PH;       // 나사 골지름(minor diameter) — Lisp gd
    const double dp = (an2 - gd) / 2.0;          // 골 깊이 — Lisp dp(=k1)
    const double hexDiag = B / qCos(qDegreesToRadians(30.0));   // 소켓 육각 대각지름(across corners) — Lisp dg
    const double hc = E / qCos(qDegreesToRadians(60.0));        // 머리 상단 모따기 길이 — Lisp hc

    // ── Lisp 점들 (변수명 그대로 유지) ─────────────────────────
    QPointF p1 = fromLisp( D/2.0, 0);
    QPointF p2 = fromLisp( D/2.0, H-E);
    QPointF p3 = fromLisp( D/2.0 - hc*qCos(qDegreesToRadians(30.0)), H);
    QPointF p4 = fromLisp(-(D/2.0 - hc*qCos(qDegreesToRadians(30.0))), H);
    QPointF p5 = fromLisp(-D/2.0, H-E);
    QPointF p6 = fromLisp(-D/2.0, 0);

    // ── 머리 외형선 (원통 벽 + 상단 모따기) ─────────────────────
    segs.append({ p1, p2, BoltLineKind::Object }); // 머리 우측 원통벽
    segs.append({ p2, p3, BoltLineKind::Object }); // 머리 상단 모따기(우)
    segs.append({ p3, p4, BoltLineKind::Object }); // 머리 상단 평면
    segs.append({ p4, p5, BoltLineKind::Object }); // 머리 상단 모따기(좌)
    segs.append({ p5, p6, BoltLineKind::Object }); // 머리 좌측 원통벽
    segs.append({ p5, p2, BoltLineKind::Object }); // 모따기 시작 높이 가로선(Lisp e8, 원본 그대로 이식)
    segs.append({ p1, p6, BoltLineKind::Object }); // 머리/몸통 경계 어깨선(머리 전체 폭)

    // ── 머리/몸통 경계 필렛로 인해 몸통벽은 어깨(y=0)가 아니라 R1만큼 아래에서 시작 ──
    QPointF wallTopR = fromLisp( an2/2.0, -R1);
    QPointF wallTopL = fromLisp(-an2/2.0, -R1);
    QPointF p11 = fromLisp( an2/2.0, -(L - dp));
    QPointF p8  = fromLisp(-an2/2.0, -(L - dp));
    QPointF p9  = fromLisp(-an2/2.0 + dp, -L);
    QPointF p10 = fromLisp( an2/2.0 - dp, -L);

    segs.append({ wallTopR, p11, BoltLineKind::Object }); // 몸통 우측벽
    segs.append({ wallTopL, p8,  BoltLineKind::Object }); // 몸통 좌측벽
    segs.append({ p8,  p11, BoltLineKind::Object, true }); // 끝단 모따기 시작선 — 골지름선 레이어(BoltTool과 동일 처리)
    segs.append({ p11, p10, BoltLineKind::Object }); // 끝단 모따기(우)
    segs.append({ p8,  p9,  BoltLineKind::Object }); // 끝단 모따기(좌)
    segs.append({ p9,  p10, BoltLineKind::Object }); // 끝단 평면

    // ── 나사 골지름 표시선 ────────────────────────────────────
    // Lisp 원본은 이 구분 없이 항상 어깨(y=0)부터 끝단까지 전나사로 그린다.
    // BoltTool과 동일하게 fullThread=false(부분나사)면 끝단에서 나사부 길이(b)만큼만
    // 그리고, 어깨~나사 시작 구간이 충분하면(2.34 이상) 런아웃 사선/연결선을 추가한다.
    double bLen = hexBoltThreadLength(an2, L);           // 나사부 길이 b
    double threadStartLen = fullThread ? L : std::min(bLen, L);
    double y13 = -(L - threadStartLen); // p13/p15의 v(Lisp Y) 위치 — 전나사면 0(어깨)

    QPointF p13 = fromLisp(-an2/2.0 + dp, y13);
    QPointF p14 = fromLisp(-an2/2.0 + dp, -L);
    QPointF p15 = fromLisp( an2/2.0 - dp, y13);
    QPointF p16 = fromLisp( an2/2.0 - dp, -L);

    if (threadStartLen > 1e-6) {
        segs.append({ p13, p14, BoltLineKind::ThreadIndicator, true });
        segs.append({ p15, p16, BoltLineKind::ThreadIndicator, true });

        if (!fullThread && (L - bLen) >= 2.34) {
            double runoutDv = dp * qTan(qDegreesToRadians(60.0));
            QPointF p13d = fromLisp(-an2/2.0, y13 + runoutDv);
            QPointF p15d = fromLisp( an2/2.0, y13 + runoutDv);
            segs.append({ p13, p13d, BoltLineKind::ThreadIndicator, true });
            segs.append({ p15, p15d, BoltLineKind::ThreadIndicator, true });

            QPointF p13c = fromLisp(-an2/2.0, y13);
            QPointF p15c = fromLisp( an2/2.0, y13);
            segs.append({ p13c, p15c, BoltLineKind::Object, false }); // 몸통을 가로지르는 외형선
        }
    }

    // ── 육각 소켓(렌치 구멍) 입구 표시선 — 머리 상단에서 소켓 깊이(M)만큼 ──
    // 렌치홈 전체(투영선+바닥 지그재그+아래 buildLocalArcs의 얕은 3점호)는 골지름선과
    // 같은 레이어에 넣는다(사용자 지정) — isThreadDiaLayer=true, 선 스타일(Object)은 유지.
    QPointF p17 = fromLisp( hexDiag/2.0, H);
    QPointF p18 = fromLisp( hexDiag/2.0, H-M);
    QPointF p19 = fromLisp( hexDiag/4.0, H);
    QPointF p20 = fromLisp( hexDiag/4.0, H-M);
    segs.append({ p17, p18, BoltLineKind::Object, true }); // 소켓 외곽 모서리 투영선(우)
    segs.append({ p19, p20, BoltLineKind::Object, true }); // 소켓 내측 모서리 투영선(우)
    segs.append({ fromLisp(-hexDiag/2.0, H), fromLisp(-hexDiag/2.0, H-M), BoltLineKind::Object, true }); // 소켓 외곽 모서리 투영선(좌)
    segs.append({ fromLisp(-hexDiag/4.0, H), fromLisp(-hexDiag/4.0, H-M), BoltLineKind::Object, true }); // 소켓 내측 모서리 투영선(좌)

    // 소켓 바닥 지그재그선(Lisp p18→p22→p25) — 육각 구멍 바닥이 살짝 보이는 것을 표현
    const double sl = (hexDiag/2.0) / qCos(qDegreesToRadians(30.0));
    QPointF p22 = fromLisp(hexDiag/2.0 + sl*qCos(qDegreesToRadians(210.0)),
                           H-M       + sl*qSin(qDegreesToRadians(210.0)));
    QPointF p25 = fromLisp(-hexDiag/2.0, H-M);
    segs.append({ p18, p22, BoltLineKind::Object, true });
    segs.append({ p22, p25, BoltLineKind::Object, true });

    // ── 입면도 중심선 (머리 위/끝단 아래로 1mm씩 더 나오게 — Lisp c1-c2) ──
    if (includeCenterline) {
        QPointF c1 = fromLisp(0, H + 1.0);
        QPointF c2 = fromLisp(0, -(L + 1.0));
        segs.append({ c1, c2, BoltLineKind::Centerline });
    }

    // ── 원형 러벤도 (평면도 — 육각형 + 중심 십자선) ────────────
    if (includeSide) {
        const double gap = 7.0;
        QPointF s1 = fromLisp(0, H + 1.0 + D/2.0 + gap); // Lisp c1 위로 (D/2+7) 만큼 더

        // 육각형(소켓) 꼭짓점 6개: 대각지름 hexDiag(circumradius=hexDiag/2), 대변거리 B
        const double R = hexDiag / 2.0;
        const double apo = B / 2.0;
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

        // 중심 십자선(Lisp c3/c4 + 90도 회전 복사 상당) — 길이는 머리 반지름(D/2)+1 기준
        if (includeCenterline) {
            double crossExt = D/2.0 + 1.0;
            segs.append({ QPointF(s1.x()-crossExt, s1.y()), QPointF(s1.x()+crossExt, s1.y()), BoltLineKind::Centerline });
            segs.append({ QPointF(s1.x(), s1.y()-crossExt), QPointF(s1.x(), s1.y()+crossExt), BoltLineKind::Centerline });
        }
    }

    return segs;
}

QList<BoltArcSpec> WrenchBoltTool::buildLocalArcs(const WrenchBoltSize &size) {
    QList<BoltArcSpec> arcs;

    const double an2 = size.d;
    const double H   = size.headH;
    const double E   = size.chamferE;
    const double B   = size.socketWidth;
    const double M   = size.socketDepth;
    const double R1  = size.filletR;

    const double hexDiag = B / qCos(qDegreesToRadians(30.0));

    // ── 머리/몸통 경계 필렛 아크(양쪽) — 몸통벽(수직)과 어깨선(수평)이 만나는
    // 직각 코너를 반지름 R1로 둥글린다. 중심은 두 접선으로부터 각각 R1 떨어진
    // (몸통벽 안쪽·어깨선 바깥쪽) 위치에 있다 (Lisp fillet 명령과 동일한 기하).
    {
        QPointF centerR = fromLisp(an2/2.0 + R1, -R1);
        QPointF tanWallR = fromLisp(an2/2.0, -R1);
        QPointF tanShoulderR = fromLisp(an2/2.0 + R1, 0);
        arcs.append({ centerR, tanWallR, tanShoulderR });

        QPointF centerL = fromLisp(-an2/2.0 - R1, -R1);
        QPointF tanWallL = fromLisp(-an2/2.0, -R1);
        QPointF tanShoulderL = fromLisp(-an2/2.0 - R1, 0);
        arcs.append({ centerL, tanWallL, tanShoulderL });
    }

    // ── 소켓 입구의 얕은 곡선(3점호) — Lisp e14/e141/e17 상당 ──────────
    {
        QPointF p18 = fromLisp( hexDiag/2.0, H-M);
        QPointF p20 = fromLisp( hexDiag/4.0, H-M);
        QPointF p20L = fromLisp(-hexDiag/4.0, H-M);
        QPointF p18L = fromLisp(-hexDiag/2.0, H-M);

        // 우측 얕은 호: p18-p20 사이, 처짐량 E
        double midXR = (hexDiag/2.0 + hexDiag/4.0) / 2.0;
        QPointF arcp2 = fromLisp(midXR, H - M - E);
        QPointF centerDipR;
        if (circumcenter3(p18, arcp2, p20, centerDipR)) {
            arcs.append({ centerDipR, p18, p20, BoltLineKind::Object, true }); // 렌치홈 일부 — 골지름선 레이어
        }

        // 중앙 얕은 호: p20-(-p20) 사이, 처짐량 E — 좌우대칭이라 별도 미러가 필요없다
        QPointF arcp3 = fromLisp(0, H - M - E);
        QPointF centerDipC;
        if (circumcenter3(p20, arcp3, p20L, centerDipC)) {
            arcs.append({ centerDipC, p20, p20L, BoltLineKind::Object, true });
        }

        // 좌측 얕은 호: 우측 호를 x=0 기준으로 미러
        double midXL = -midXR;
        QPointF arcp2L = fromLisp(midXL, H - M - E);
        QPointF centerDipL;
        if (circumcenter3(p18L, arcp2L, p20L, centerDipL)) {
            arcs.append({ centerDipL, p18L, p20L, BoltLineKind::Object, true });
        }
    }

    return arcs;
}

// 원형 러벤도에 그릴 머리 외곽원/모따기원. Lisp에서는 "머리 지름(D)" 원과
// "모따기 후 남는 폭(distance p4 p3)" 원 두 개를 그린다.
QList<BoltCircleSpec> WrenchBoltTool::buildLocalCircles(const WrenchBoltSize &size, bool includeSide) {
    QList<BoltCircleSpec> circles;
    if (!includeSide) return circles;

    const double D = size.headD;
    const double H = size.headH;
    const double E = size.chamferE;
    const double gap = 7.0;
    QPointF s1 = fromLisp(0, H + 1.0 + D/2.0 + gap);

    circles.append({ s1, D/2.0 });                                   // 머리 외곽원
    circles.append({ s1, (D - 2.0*qSqrt(3.0)*E) / 2.0 });             // 상단 모따기 후 남는 원(distance p4 p3)
    return circles;
}

QPainterPath WrenchBoltTool::buildWorldPath(const WrenchBoltSize &size, double length,
                                            const QPointF &origin, double angleDeg, bool fullThread,
                                            bool includeCenterline, bool includeSide) {
    return buildBoltWorldPath(buildLocalSegments(size, length, fullThread, includeCenterline, includeSide),
                              buildLocalArcs(size),
                              buildLocalCircles(size, includeSide),
                              origin, angleDeg);
}

QList<QGraphicsItem *> WrenchBoltTool::buildGeometry(const WrenchBoltSize &size, double length,
                                                     const QPointF &origin, double angleDeg,
                                                     bool fullThread,
                                                     bool includeCenterline, bool includeSide) {
    // 렌치홈(육각 소켓 입구)은 인접한 선/호끼리 끝점이 정확히 겹쳐서 이어지는 하나의
    // 고정 기호라, 그립을 켜두면 클릭이 옆 도형의 그립으로 잘못 잡혀 미세 드래그되면서
    // 모양이 틀어지는 문제가 있었다. 이 툴이 만드는 형상은 전부 파라메트릭 재배치로만
    // 바뀌어야 하므로(개별 점 편집 대상 아님) 전체 아이템에 그립 편집 자체를 꺼둔다.
    return buildBoltGraphicsItems(buildLocalSegments(size, length, fullThread, includeCenterline, includeSide),
                                  buildLocalArcs(size),
                                  buildLocalCircles(size, includeSide),
                                  origin, angleDeg, /*gripsEnabled=*/false);
}
