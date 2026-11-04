#include "hhbolttool.h"
#include "hhboltsetupdialog.h"
#include "boltgeometry.h"
#include "iappcontext.h"
#include "tooldefaults.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

// 3-2: 도구 기본값은 ToolDefaults(도면 스코프 우선). 네임스페이스 "HHBoltTool".

HHBoltTool::HHBoltTool(QObject *parent)
    : AbstractBoltPlacementTool(parent), m_fullThread(true)
{}

void HHBoltTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);

    // 마지막으로 사용한 호칭경/길이/레이어(외형선·골지름선)/2점지정여부/전나사여부를 다음 실행에도 유지한다.
    ToolDefaults bd("HHBoltTool", scene);
    QString savedDiameter     = bd.getString("Diameter", "M10");
    double  savedLength       = bd.getDouble("Length", 40.0);
    QString savedOutlineLayer = bd.getString("OutlineLayer", "");
    QString savedThreadLayer  = bd.getString("ThreadLayer", "");
    bool    savedTwoPointMode = bd.getBool("TwoPointMode", false);
    bool    savedFullThread   = bd.getBool("FullThread", true);
    bool    savedCenterline   = bd.getBool("CenterlineEnabled", true);
    bool    savedSideView     = bd.getBool("SideViewEnabled", true);

    IAppContext *ctx = getContext();
    HHBoltSetupDialog dlg(ctx);
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

    finishActivation(tr("접시머리렌치볼트 배치 (%1 x %2)").arg(m_size.label).arg(m_length));
}

QString HHBoltTool::positionPromptText() const {
    return tr(" 접시머리렌치볼트 삽입 기준점(머리 상단 중심, Lisp의 \"Pick upper center\") 지정: ");
}

QString HHBoltTool::toolMacroLabel() const {
    return tr("접시머리렌치볼트 배치");
}

QList<QGraphicsItem *> HHBoltTool::buildPlacementGeometry(double length, const QPointF &origin,
                                                           double angleDeg) const {
    return buildGeometry(m_size, length, origin, angleDeg, m_fullThread, m_centerlineEnabled, m_sideViewEnabled);
}

QPainterPath HHBoltTool::buildPlacementPreviewPath(double length, const QPointF &origin,
                                                    double angleDeg) const {
    return buildWorldPath(m_size, length, origin, angleDeg, m_fullThread, m_centerlineEnabled, m_sideViewEnabled);
}

// ─────────────────────────────────────────────────────────────────────────
// 아래 buildLocalSegments/buildLocalArcs/buildLocalCircles 는
// Lisp\hhbolt.lsp 의 (defun c:hhbolt ...) 알고리즘을 그대로 이식한 것이다. 로컬
// 좌표계는 Lisp의 pt(="Pick upper center", 즉 접시머리 상단면 중심)를 원점으로
// 두고, Lisp의 +Y(머리쪽)를 내 -v로, Lisp의 -Y(끝단쪽)를 내 +v로 대응시킨다
// (Lisp_Y → v = -Lisp_Y, X는 그대로 u). BoltTool/WrenchBoltTool과 동일한 규약.
// fromLisp/circumcenter3는 boltgeometry.h의 공유 함수를 그대로 쓴다.
// ─────────────────────────────────────────────────────────────────────────

QList<BoltSegment> HHBoltTool::buildLocalSegments(const HHBoltSize &size, double length,
                                                   bool fullThread, bool includeCenterline,
                                                   bool includeSide) {
    QList<BoltSegment> segs;

    const double an2 = size.d;           // 호칭경(나사 바깥지름) — Lisp an2
    const double PH  = size.pitch;       // Lisp ph
    const double K   = size.tipChamferK; // 끝단 모따기 높이 — Lisp k
    const double A   = size.headD;       // 접시머리 지름 — Lisp a
    const double H   = size.headH;       // 머리 높이(원추 깊이) — Lisp h
    const double M   = size.socketDepth; // 육각 소켓 깊이 — Lisp m
    const double C   = size.cornerH;     // 머리 상단 모서리 높이 — Lisp c
    const double G   = size.socketWidth; // 육각 소켓 대변거리 — Lisp g
    const double bl  = length;           // 전체 길이(머리 상단 기준) — Lisp bl

    const double gd = an2 - 1.082532 * PH;                      // 나사 골지름(minor diameter) — Lisp gd
    const double dp = (an2 - gd) / 2.0;                         // 골 깊이(런아웃 계산용)
    const double dg = G / qCos(qDegreesToRadians(30.0));        // 소켓 육각 대각지름(across corners) — Lisp dg

    // ── Lisp 점들 (변수명 그대로 유지) ─────────────────────────
    QPointF p4 = fromLisp( A/2.0, 0);
    QPointF p5 = fromLisp(-A/2.0, 0);
    QPointF p6 = fromLisp(-A/2.0, -C);
    QPointF p7 = fromLisp( A/2.0, -C);
    QPointF p8 = fromLisp( an2/2.0, -H);
    QPointF p9 = fromLisp(-an2/2.0, -H);

    // ── 머리(접시머리) 외형선 ────────────────────────────────
    segs.append({ p4, p5, BoltLineKind::Object }); // 머리 상단 평면(전체 폭 A)
    segs.append({ p4, p7, BoltLineKind::Object }); // 머리 상단 모서리 수직단(우) — 높이 C
    segs.append({ p6, p5, BoltLineKind::Object }); // 머리 상단 모서리 수직단(좌)
    segs.append({ p7, p6, BoltLineKind::Object }); // 모서리 하단 가로선(코너 원둘레의 투영)
    segs.append({ p7, p8, BoltLineKind::Object }); // 원추(접시) 사면(우)
    segs.append({ p6, p9, BoltLineKind::Object }); // 원추(접시) 사면(좌)
    segs.append({ p8, p9, BoltLineKind::Object }); // 머리/몸통 경계 어깨선(머리 전체 폭)

    // ── 몸통/끝단 외형선 ─────────────────────────────────────
    QPointF p11 = fromLisp( gd/2.0, -bl);
    QPointF p12 = fromLisp(-gd/2.0, -bl);
    QPointF p14 = fromLisp( an2/2.0, -(bl - K));
    QPointF p15 = fromLisp(-an2/2.0, -(bl - K));

    segs.append({ p8,  p14, BoltLineKind::Object }); // 몸통 우측벽
    segs.append({ p9,  p15, BoltLineKind::Object }); // 몸통 좌측벽
    segs.append({ p14, p15, BoltLineKind::Object, true }); // 끝단 모따기 시작선 — 골지름선 레이어(BoltTool과 동일 처리)
    segs.append({ p14, p11, BoltLineKind::Object }); // 끝단 모따기(우)
    segs.append({ p15, p12, BoltLineKind::Object }); // 끝단 모따기(좌)
    segs.append({ p11, p12, BoltLineKind::Object }); // 끝단 평면

    // ── 나사 골지름 표시선 ────────────────────────────────────
    // Lisp 원본은 이 구분 없이 항상 어깨(y=-H)부터 끝단(y=-bl)까지 전나사로 그린다
    // (규격표의 sl 컬럼은 정의만 되어있고 실제로는 쓰이지 않았다). BoltTool/WrenchBoltTool과
    // 동일하게 fullThread=false(부분나사)면 끝단에서 나사부 길이(b)만큼만 그리고, 어깨~나사
    // 시작 구간이 충분하면(2.34 이상) 런아웃 사선/연결선을 추가한다.
    const double shankLen = bl - H;                              // 몸통(머리 제외) 길이
    const double bLen = hexBoltThreadLength(an2, bl);            // 나사부 길이 b — 호칭길이(머리 상단 기준) 그대로 사용
    const double threadStartLen = fullThread ? shankLen : std::min(bLen, shankLen);
    const double y13 = -bl + threadStartLen;                     // 나사 표시선이 시작되는 Lisp y — 전나사면 어깨(-H)

    if (threadStartLen > 1e-6) {
        QPointF p13 = fromLisp( gd/2.0, y13);
        QPointF p16 = fromLisp(-gd/2.0, y13);
        segs.append({ p13, p11, BoltLineKind::ThreadIndicator, true });
        segs.append({ p16, p12, BoltLineKind::ThreadIndicator, true });

        if (!fullThread && (shankLen - bLen) >= 2.34) {
            double runoutDv = dp * qTan(qDegreesToRadians(60.0));
            QPointF p13d = fromLisp( an2/2.0, y13 + runoutDv);
            QPointF p16d = fromLisp(-an2/2.0, y13 + runoutDv);
            segs.append({ p13, p13d, BoltLineKind::ThreadIndicator, true });
            segs.append({ p16, p16d, BoltLineKind::ThreadIndicator, true });

            QPointF p13c = fromLisp( an2/2.0, y13);
            QPointF p16c = fromLisp(-an2/2.0, y13);
            segs.append({ p16c, p13c, BoltLineKind::Object, false }); // 몸통을 가로지르는 외형선
        }
    }

    // ── 육각 소켓(렌치 구멍) 입구 표시선 — 머리 상단면에서 소켓 깊이(M)만큼 ──
    // 렌치홈 전체(투영선+바닥 지그재그+드릴 자국선+아래 buildLocalArcs의 얕은 3점호)는
    // 골지름선과 같은 레이어에 넣는다(WrenchBoltTool과 동일한 처리) — isThreadDiaLayer=true.
    QPointF p22 = fromLisp( dg/2.0, 0);
    QPointF p23 = fromLisp( dg/2.0, -M);
    QPointF p20 = fromLisp( dg/4.0, 0);
    QPointF p21 = fromLisp( dg/4.0, -M);
    segs.append({ p22, p23, BoltLineKind::Object, true }); // 소켓 외곽 모서리 투영선(우)
    segs.append({ p20, p21, BoltLineKind::Object, true }); // 소켓 내측 모서리 투영선(우)
    segs.append({ fromLisp(-dg/2.0, 0), fromLisp(-dg/2.0, -M), BoltLineKind::Object, true }); // 소켓 외곽 모서리 투영선(좌)
    segs.append({ fromLisp(-dg/4.0, 0), fromLisp(-dg/4.0, -M), BoltLineKind::Object, true }); // 소켓 내측 모서리 투영선(좌)

    // 소켓 바닥이 드릴 끝점(pt1, 어깨 바로 위)을 향해 좁아드는 모습을 나타내는 대각선
    // (실제 육각 소켓은 바닥이 평평하지 않고 드릴 끝의 원뿔 자국이 살짝 남는 것을 표현)
    QPointF pt1 = fromLisp(0, -H + 0.2);
    segs.append({ pt1, fromLisp(-dg/2.0, -M), BoltLineKind::Object, true });
    segs.append({ pt1, fromLisp( dg/2.0, -M), BoltLineKind::Object, true });

    // ── 입면도 중심선 (머리 위/끝단 아래로 2.5mm씩 더 나오게 — Lisp p18-p19) ──
    if (includeCenterline) {
        QPointF c1 = fromLisp(0, 2.5);
        QPointF c2 = fromLisp(0, -(bl + 2.5));
        segs.append({ c1, c2, BoltLineKind::Centerline });
    }

    // ── 원형 러벤도 (평면도 — 육각형 + 중심 십자선) ────────────
    if (includeSide) {
        const double gap = 10.0;
        QPointF s1 = fromLisp(0, A/2.0 + gap); // Lisp s1: 머리 위로 (A/2+10) 만큼 떨어진 자리

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

        // 중심 십자선(Lisp c1/c2 + 90도 회전 복사 상당) — 길이는 머리 반지름(A/2)+1.5 기준
        if (includeCenterline) {
            double crossExt = A/2.0 + 1.5;
            segs.append({ QPointF(s1.x()-crossExt, s1.y()), QPointF(s1.x()+crossExt, s1.y()), BoltLineKind::Centerline });
            segs.append({ QPointF(s1.x(), s1.y()-crossExt), QPointF(s1.x(), s1.y()+crossExt), BoltLineKind::Centerline });
        }
    }

    return segs;
}

QList<BoltArcSpec> HHBoltTool::buildLocalArcs(const HHBoltSize &size) {
    QList<BoltArcSpec> arcs;

    const double G = size.socketWidth;
    const double M = size.socketDepth;
    const double C = size.cornerH; // 소켓 바닥 처짐량으로도 재사용(Lisp 원본과 동일)

    const double dg = G / qCos(qDegreesToRadians(30.0));

    // ── 소켓 입구의 얕은 곡선(3점호) — Lisp e17/e17a/e21a 상당 ──────────
    QPointF p23 = fromLisp( dg/2.0, -M);
    QPointF p21 = fromLisp( dg/4.0, -M);
    QPointF p20L = fromLisp(-dg/4.0, -M);
    QPointF p23L = fromLisp(-dg/2.0, -M);

    // 좌측 얕은 호: p23L-p20L 사이, 처짐량 C
    double midXL = -(dg/2.0 + dg/4.0) / 2.0;
    QPointF dipL = fromLisp(midXL, -M - C);
    QPointF centerDipL;
    if (circumcenter3(p23L, dipL, p20L, centerDipL)) {
        arcs.append({ centerDipL, p23L, p20L, BoltLineKind::Object, true }); // 렌치홈 일부 — 골지름선 레이어
    }

    // 중앙 얕은 호: p20L-p21 사이, 처짐량 C — 좌우대칭이라 별도 미러가 필요없다
    QPointF dipC = fromLisp(0, -M - C);
    QPointF centerDipC;
    if (circumcenter3(p20L, dipC, p21, centerDipC)) {
        arcs.append({ centerDipC, p20L, p21, BoltLineKind::Object, true });
    }

    // 우측 얕은 호: 좌측 호를 x=0 기준으로 미러
    double midXR = -midXL;
    QPointF dipR = fromLisp(midXR, -M - C);
    QPointF centerDipR;
    if (circumcenter3(p21, dipR, p23, centerDipR)) {
        arcs.append({ centerDipR, p21, p23, BoltLineKind::Object, true });
    }

    return arcs;
}

// 원형 러벤도에 그릴 머리 외곽원. Lisp에서는 접시머리 지름(A) 원 하나만 그린다.
QList<BoltCircleSpec> HHBoltTool::buildLocalCircles(const HHBoltSize &size, bool includeSide) {
    QList<BoltCircleSpec> circles;
    if (!includeSide) return circles;

    const double A = size.headD;
    const double gap = 10.0;
    QPointF s1 = fromLisp(0, A/2.0 + gap);

    circles.append({ s1, A/2.0 }); // 머리 외곽원
    return circles;
}

QPainterPath HHBoltTool::buildWorldPath(const HHBoltSize &size, double length,
                                        const QPointF &origin, double angleDeg, bool fullThread,
                                        bool includeCenterline, bool includeSide) {
    return buildBoltWorldPath(buildLocalSegments(size, length, fullThread, includeCenterline, includeSide),
                              buildLocalArcs(size),
                              buildLocalCircles(size, includeSide),
                              origin, angleDeg);
}

QList<QGraphicsItem *> HHBoltTool::buildGeometry(const HHBoltSize &size, double length,
                                                 const QPointF &origin, double angleDeg,
                                                 bool fullThread,
                                                 bool includeCenterline, bool includeSide) {
    return buildBoltGraphicsItems(buildLocalSegments(size, length, fullThread, includeCenterline, includeSide),
                                  buildLocalArcs(size),
                                  buildLocalCircles(size, includeSide),
                                  origin, angleDeg, /*gripsEnabled=*/false);
}
