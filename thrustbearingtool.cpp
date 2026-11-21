#include "thrustbearingtool.h"
#include "bearinglayer.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "drawingsettingscommand.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layer.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include "bearingrotation.h"
#include "thrustbearingdialog.h"
#include "tooldefaults.h"

#include <QKeyEvent>
#include <QPen>
#include <QTimer>
#include <QUndoStack>
#include <QtMath>

namespace {
const char *const kToolNamespace = "ThrustBearingTool";
const char *const kLayerBody   = "BEARING";
const char *const kLayerCenter = "BEARING-CEN";
const char *const kLayerDim    = "BEARING-DIM";

// 닫힌 윤곽의 한 꼭짓점. (a, h) = (반지름 방향, 축 방향) 로컬 좌표.
//   r     : 모서리 필렛 반경 (0 이면 뾰족)
//   theta : 이 꼭짓점에서 다음 꼭짓점으로 가는 변이 호이면 그 중심각(rad), 직선이면 0
//   kind  : 호의 방향 +1 = 재료 바깥으로 볼록, -1 = 재료 안쪽으로 오목
struct LV {
    double a;
    double h;
    double r;
    double theta;
    int    kind;
};

struct Loop {
    ThrustSection  sec;
    QList<QPointF> tanPrev;   // 꼭짓점별: 이전 변 쪽 접점 (필렛 없으면 꼭짓점)
    QList<QPointF> tanNext;   // 꼭짓점별: 다음 변 쪽 접점
};

// 로컬 좌표 → 씬 좌표 (축선 x=ox, 베어링 높이 중앙이 oy)
struct Frame {
    double ox;
    double oy;
    double hMid;
    QPointF at(double a, double h) const { return QPointF(ox + a, oy + h - hMid); }
};

double crossOf(const QPointF &a, const QPointF &b) { return a.x() * b.y() - a.y() * b.x(); }
double dotOf(const QPointF &a, const QPointF &b)   { return a.x() * b.x() + a.y() * b.y(); }
double lenOf(const QPointF &a)                      { return qSqrt(dotOf(a, a)); }

// 폴리라인(bulge 포함)을 선분으로 근사한 닫힌 경로 (해칭용)
QPainterPath tessellate(const std::vector<QPointF> &pts, const std::vector<double> &bulges) {
    QPainterPath path;
    const int n = int(pts.size());
    if (n < 2) return path;
    path.moveTo(pts[0]);
    for (int i = 0; i < n; ++i) {
        const QPointF p = pts[i];
        const QPointF q = pts[(i + 1) % n];
        const double b = bulges[i];
        if (qAbs(b) < 1e-9) {
            if (i < n - 1) path.lineTo(q);
            continue;
        }
        const double theta = 4.0 * qAtan(b);
        const QPointF d = q - p;
        const double L = lenOf(d);
        if (L < 1e-12) continue;
        const double half = qAbs(theta) * 0.5;
        const double R = L / (2.0 * qSin(half));
        const QPointF mid = (p + q) * 0.5;
        const QPointF nrm(-d.y() / L, d.x() / L);
        const double dist = R * qCos(half);
        const QPointF c = mid + nrm * (b > 0.0 ? dist : -dist);
        const double a0 = qAtan2(p.y() - c.y(), p.x() - c.x());
        const int steps = qMax(4, int(qAbs(theta) / (M_PI / 12.0)) + 1);
        for (int s = 1; s < steps; ++s) {
            const double ang = a0 + theta * s / steps;
            path.lineTo(c.x() + R * qCos(ang), c.y() + R * qSin(ang));
        }
        if (i < n - 1) path.lineTo(q);
    }
    path.closeSubpath();
    return path;
}

// 꼭짓점 목록(오른쪽 반 기준 반시계 순서)으로 필렛·호가 들어간 닫힌 폴리라인을 만든다.
// sg = -1 이면 a 를 뒤집어 왼쪽 반을 만든다 (방향은 면적 부호로 다시 판정).
Loop makeLoop(const QList<LV> &v, double sg, const Frame &f) {
    Loop loop;
    const int n = v.size();
    std::vector<QPointF> P;
    for (int i = 0; i < n; ++i) P.push_back(f.at(sg * v.at(i).a, v.at(i).h));

    double area = 0.0;
    for (int i = 0; i < n; ++i) area += crossOf(P[i], P[(i + 1) % n]);
    const double orient = (area >= 0.0) ? 1.0 : -1.0;

    std::vector<QPointF> pts;
    std::vector<double> bulges;
    for (int i = 0; i < n; ++i) {
        const LV &cur = v.at(i);
        const LV &prv = v.at((i + n - 1) % n);
        const QPointF prev = P[(i + n - 1) % n];
        const QPointF next = P[(i + 1) % n];
        const QPointF c = P[i];
        const bool curArc  = cur.theta > 1e-9;
        const bool prevArc = prv.theta > 1e-9;

        bool filleted = false;
        if (cur.r > 0.0 && !curArc && !prevArc) {
            const QPointF e1 = prev - c;
            const QPointF e2 = next - c;
            const double l1 = lenOf(e1);
            const double l2 = lenOf(e2);
            if (l1 > 1e-9 && l2 > 1e-9) {
                const QPointF u = e1 / l1;
                const QPointF w = e2 / l2;
                double cosPhi = dotOf(u, w);
                cosPhi = qBound(-1.0, cosPhi, 1.0);
                const double phi = qAcos(cosPhi);
                if (phi > 0.05 && phi < M_PI - 0.02) {
                    double t = cur.r / qTan(phi * 0.5);
                    const double tMax = 0.45 * qMin(l1, l2);
                    if (t > tMax) t = tMax;
                    const QPointF a = c + u * t;
                    const QPointF b = c + w * t;
                    const double turn = crossOf(c - prev, next - c);
                    const double bulge = qTan((M_PI - phi) * 0.25) * (turn >= 0.0 ? 1.0 : -1.0);
                    pts.push_back(a);
                    bulges.push_back(bulge);
                    pts.push_back(b);
                    bulges.push_back(0.0);
                    loop.tanPrev.append(a);
                    loop.tanNext.append(b);
                    filleted = true;
                }
            }
        }
        if (!filleted) {
            pts.push_back(c);
            bulges.push_back(curArc ? cur.kind * orient * qTan(cur.theta * 0.25) : 0.0);
            loop.tanPrev.append(c);
            loop.tanNext.append(c);
        }
    }

    for (size_t i = 0; i < pts.size(); ++i) {
        loop.sec.outline.push_back(MyVector(pts[i]));
    }
    loop.sec.bulges = bulges;
    loop.sec.hatchPath = tessellate(pts, bulges);
    return loop;
}

MyDimension makeVerticalDim(const QPointF &top, const QPointF &bottom, double lineX) {
    MyDimension d{MyVector(top), MyVector(bottom), MyVector(QPointF(lineX, (top.y() + bottom.y()) * 0.5)),
                  QString()};
    d.dimensionType = DimensionType::Linear;
    d.useFixedAngle = true;
    d.fixedAngle    = 90.0;
    return d;
}

MyDimension makeHorizontalDim(const QPointF &a, const QPointF &b, double lineY) {
    MyDimension d{MyVector(a), MyVector(b), MyVector(QPointF((a.x() + b.x()) * 0.5, lineY)), QString()};
    d.dimensionType = DimensionType::Linear;
    d.useFixedAngle = true;
    d.fixedAngle    = 0.0;
    return d;
}
}

namespace {
void rotateGeometry(ThrustBearingGeometry &g, const QPointF &o, double deg) {
    if (qAbs(deg) < 1e-9) return;
    BearingRotation::Rot r(o, deg);
    for (int i = 0; i < g.sections.size(); ++i) {
        BearingRotation::rotVecs(g.sections[i].outline, r);
        BearingRotation::rotPath(g.sections[i].hatchPath, r);
    }
    for (int i = 0; i < g.balls.size(); ++i) g.balls[i].center = r.pt(g.balls[i].center);
    BearingRotation::rotLines(g.lines, r);
    BearingRotation::rotLines(g.centerLines, r);
    BearingRotation::rotDims(g.dims, r, deg);
}
}

namespace {
QList<LV> housingVerts(double A, double B, double t, double a1, double a2, double c,
                       double delta, double thetaS, bool spherical) {
    QList<LV> v;
    if (spherical) v << LV{A, -delta, 0.0, thetaS, +1} << LV{B, 0.0, 0.0, 0.0, 0};
    else           v << LV{A, 0.0, c, 0.0, 0} << LV{B, 0.0, c, 0.0, 0};
    v << LV{B, t, c, 0.0, 0} << LV{a2, t, 0.0, M_PI / 3.0, -1} << LV{a1, t, 0.0, 0.0, 0}
      << LV{A, t, c, 0.0, 0};
    return v;
}

QList<LV> mirrorVerts(const QList<LV> &v, double H) {
    QList<LV> out;
    for (int i = 0; i < v.size(); ++i) {
        LV m = v.at(i);
        m.h = H - m.h;
        out.append(m);
    }
    return out;
}

// 복식: 가운데 와셔를 공유하는 양방향 2단 볼. 위아래 대칭이라 기준점은 h = H/2.
ThrustBearingGeometry buildDouble(const ThrustBearingParams &p, const QPointF &o) {
    ThrustBearingGeometry g;
    if (p.d <= 0.0 || p.di <= 0.0 || p.D <= p.d || p.D <= p.di || p.a <= 0.0 || p.H <= p.a) return g;

    const double A2 = p.d * 0.5;     // 바깥 두 와셔 안반지름
    const double Am = p.di * 0.5;    // 가운데 와셔 안반지름
    const double B  = p.D * 0.5;
    const double H  = p.H;
    const double a  = p.a;
    const double Hs = (H - a) / 1.433;                    // 한쪽 단을 단식 높이로 환산
    const double rb = Hs * 0.25;
    const double t  = Hs * 0.5 - rb * qCos(M_PI / 6.0);   // 바깥 와셔 두께
    const double gap = Hs - 2.0 * t;                      // 면과 면 사이 간격
    const double m0 = t + gap;                            // 가운데 와셔 아랫면
    const double m1 = m0 + a;                             // 가운데 와셔 윗면
    const double hb1 = t + gap * 0.5;
    const double hb2 = m1 + gap * 0.5;
    const double yc = (p.D + p.d) * 0.25;
    const double g8 = Hs * 0.125;
    const double a1 = yc - g8;
    const double a2 = yc + g8;
    const double c  = qMax(0.0, p.c);
    const double c1 = qMax(0.0, p.c1);

    double delta = 0.0;
    double thetaS = 0.0;
    bool spherical = false;
    if (p.type >= 1 && p.H1 > H + 0.02) {
        delta = (p.H1 - H) * 0.5;
        const double u = (B * B - A2 * A2 - delta * delta) / (2.0 * delta);
        if (u > 0.0) {
            const double Rs = qSqrt(u * u + B * B);
            thetaS = qAsin(B / Rs) - qAsin(A2 / Rs);
            spherical = thetaS > 1e-6;
        }
    }
    const bool hasSeat = spherical && p.type == 2;
    double hSeat = 0.0;
    if (hasSeat) hSeat = -delta - qMax(0.3, (p.H2 - p.H1) * 0.5);
    const double hBot = hasSeat ? hSeat : (spherical ? -delta : 0.0);
    const double hTop = H - hBot;
    const Frame f{o.x(), o.y(), H * 0.5};

    const QList<LV> outerB = housingVerts(A2, B, t, a1, a2, c, delta, thetaS, spherical);
    const QList<LV> outerT = mirrorVerts(outerB, H);
    QList<LV> mid;
    mid << LV{Am, m0, c1, 0.0, 0} << LV{a1, m0, 0.0, M_PI / 3.0, -1} << LV{a2, m0, 0.0, 0.0, 0}
        << LV{B, m0, c1, 0.0, 0} << LV{B, m1, c1, 0.0, 0}
        << LV{a2, m1, 0.0, M_PI / 3.0, -1} << LV{a1, m1, 0.0, 0.0, 0} << LV{Am, m1, c1, 0.0, 0};
    QList<LV> seatB;
    if (hasSeat) {
        seatB << LV{A2, hSeat, c, 0.0, 0} << LV{B, hSeat, c, 0.0, 0}
              << LV{B, 0.0, 0.0, thetaS, -1} << LV{A2, -delta, 0.0, 0.0, 0};
    }
    const QList<LV> seatT = mirrorVerts(seatB, H);

    const double kSg[2] = {1.0, -1.0};
    Loop ob[2], ot[2], md[2], sb[2], st[2];
    for (int k = 0; k < 2; ++k) {
        ob[k] = makeLoop(outerB, kSg[k], f);
        ot[k] = makeLoop(outerT, kSg[k], f);
        md[k] = makeLoop(mid, kSg[k], f);
        if (hasSeat) {
            sb[k] = makeLoop(seatB, kSg[k], f);
            st[k] = makeLoop(seatT, kSg[k], f);
        }
    }
    for (int k = 0; k < 2; ++k) {
        g.sections.append(ob[k].sec);
        g.sections.append(md[k].sec);
        g.sections.append(ot[k].sec);
        if (hasSeat) {
            g.sections.append(sb[k].sec);
            g.sections.append(st[k].sec);
        }
    }

    for (int k = 0; k < 2; ++k) {
        for (int j = 0; j < 2; ++j) {
            ThrustBall b;
            b.center = f.at(kSg[k] * yc, j == 0 ? hb1 : hb2);
            b.radius = rb;
            g.balls.append(b);
        }
    }

    if (p.drawFull) {
        g.lines.append(QLineF(ob[0].tanPrev.at(5), ob[1].tanPrev.at(5)));   // 아래 바깥 와셔 윗면
        g.lines.append(QLineF(ot[0].tanPrev.at(5), ot[1].tanPrev.at(5)));   // 위 바깥 와셔 아랫면
        g.lines.append(QLineF(md[0].tanNext.at(0), md[1].tanNext.at(0)));   // 가운데 와셔 아랫면
        g.lines.append(QLineF(md[0].tanPrev.at(7), md[1].tanPrev.at(7)));   // 가운데 와셔 윗면
        g.lines.append(QLineF(ob[0].tanNext.at(0), ob[1].tanNext.at(0)));   // 아래 면 (구면이면 안쪽 모서리)
        g.lines.append(QLineF(ot[0].tanNext.at(0), ot[1].tanNext.at(0)));   // 위 면
        if (hasSeat) {
            g.lines.append(QLineF(sb[0].tanNext.at(0), sb[1].tanNext.at(0)));
            g.lines.append(QLineF(st[0].tanNext.at(0), st[1].tanNext.at(0)));
        }
    }

    if (p.drawCenter) {
        g.centerLines.append(QLineF(f.at(0.0, hBot - 0.1 * H), f.at(0.0, hTop + 0.1 * H)));
        for (int k = 0; k < 2; ++k) {
            const double cx = kSg[k] * yc;
            for (int j = 0; j < 2; ++j) {
                const double hb = (j == 0) ? hb1 : hb2;
                g.centerLines.append(QLineF(f.at(cx - 0.3 * Hs, hb), f.at(cx + 0.3 * Hs, hb)));
                g.centerLines.append(QLineF(f.at(cx, hb - 0.3 * Hs), f.at(cx, hb + 0.3 * Hs)));
            }
        }
    }

    if (p.drawDims) {
        const double gapD = qMax(p.D * 0.12, 5.0);
        MyDimension dTop = makeHorizontalDim(f.at(-A2, hTop), f.at(A2, hTop), f.at(0.0, hTop + gapD).y());
        dTop.prefix = QStringLiteral("Ø");
        g.dims.append(dTop);
        const double hRim = (spherical && !hasSeat) ? 0.0 : hBot;
        MyDimension dD = makeHorizontalDim(f.at(-B, hRim), f.at(B, hRim), f.at(0.0, hBot - gapD).y());
        dD.prefix = QStringLiteral("Ø");
        g.dims.append(dD);
        // 가운데 와셔 안지름 di
        MyDimension dMid = makeHorizontalDim(f.at(-Am, m1), f.at(Am, m1), f.at(0.0, hTop + gapD * 1.9).y());
        dMid.prefix = QStringLiteral("Ø");
        g.dims.append(dMid);
        // 오른쪽: 전체 높이 (H / H1 / H2)
        const bool sphNoSeat = spherical && !hasSeat;
        const double xLow = sphNoSeat ? A2 : B;
        const double hLow = sphNoSeat ? -delta : hBot;
        const double hHigh = sphNoSeat ? H + delta : hTop;
        g.dims.append(makeVerticalDim(f.at(xLow, hHigh), f.at(xLow, hLow), f.at(B + gapD, 0.0).x()));
    }
    return g;
}
}

ThrustBearingTool::ThrustBearingTool(QObject *parent)
    : AbstractTool(parent)
{}

// BE3.LSP 의 비례를 따른다: 볼 지름 = H/2, 볼 중심 반지름 = (D+d)/4, 와셔 두께 t = H/2 - (H/4)cos30°.
// 볼과 와셔 면이 만나는 홈은 볼 원의 30° 지점(반현 H/8) 사이 호로 그린다.
ThrustBearingGeometry ThrustBearingTool::build(const ThrustBearingParams &p, const QPointF &o) {
    if (p.dbl) return buildDouble(p, o);
    ThrustBearingGeometry g;
    if (p.d <= 0.0 || p.de <= 0.0 || p.D <= p.de || p.D <= p.d || p.H <= 0.0) return g;

    const double A  = p.de * 0.5;   // 하우징 와셔 안반지름
    const double A2 = p.d * 0.5;    // 축 와셔 안반지름
    const double B  = p.D * 0.5;    // 바깥 반지름
    const double H  = p.H;
    const double rb = H * 0.25;                          // 볼 반지름
    const double t  = H * 0.5 - rb * qCos(M_PI / 6.0);   // 와셔 두께
    const double yc = (p.D + p.d) * 0.25;                // 볼 중심 반지름
    const double g8 = H * 0.125;                         // 홈 반현
    const double a1 = yc - g8;
    const double a2 = yc + g8;
    const double c  = qMax(0.0, p.c);

    // 조심자리(구면) 계산: 하우징 와셔 바닥이 림(h=0)에서 안쪽 모서리 쪽으로 δ 만큼 내려온다.
    double delta = 0.0;
    double thetaS = 0.0;
    bool spherical = false;
    if (p.type >= 1 && p.H1 > H + 0.01) {
        delta = p.H1 - H;
        const double u = (B * B - A * A - delta * delta) / (2.0 * delta);
        if (u > 0.0) {
            const double Rs = qSqrt(u * u + B * B);
            thetaS = qAsin(B / Rs) - qAsin(A / Rs);
            spherical = thetaS > 1e-6;
        }
    }
    const bool hasSeat = spherical && p.type == 2;
    double hSeat = 0.0;   // 조심자리 와셔 바닥
    if (hasSeat) {
        hSeat = H - p.H2;
        if (hSeat > -delta - 0.3) hSeat = -delta - 0.3;
    }
    const double hBot = hasSeat ? hSeat : (spherical ? -delta : 0.0);
    const Frame f{o.x(), o.y(), (hBot + H) * 0.5};

    const double grooveTheta = M_PI / 3.0;   // 볼 홈 호 중심각 (30° 지점 사이 = 60°)

    // 하우징 와셔 (바닥 쪽, h 0..t)
    QList<LV> housing;
    if (spherical) {
        housing << LV{A, -delta, 0.0, thetaS, +1} << LV{B, 0.0, 0.0, 0.0, 0};
    } else {
        housing << LV{A, 0.0, c, 0.0, 0} << LV{B, 0.0, c, 0.0, 0};
    }
    housing << LV{B, t, c, 0.0, 0}
            << LV{a2, t, 0.0, grooveTheta, -1}
            << LV{a1, t, 0.0, 0.0, 0}
            << LV{A, t, c, 0.0, 0};
    // 축 와셔 (위쪽, h H-t..H)
    QList<LV> shaft;
    shaft << LV{A2, H - t, c, 0.0, 0}
          << LV{a1, H - t, 0.0, grooveTheta, -1}
          << LV{a2, H - t, 0.0, 0.0, 0}
          << LV{B, H - t, c, 0.0, 0}
          << LV{B, H, c, 0.0, 0}
          << LV{A2, H, c, 0.0, 0};
    // 조심자리 와셔 (구면 오목 자리, 바닥 평평)
    QList<LV> seat;
    if (hasSeat) {
        seat << LV{A, hSeat, c, 0.0, 0} << LV{B, hSeat, c, 0.0, 0}
             << LV{B, 0.0, 0.0, thetaS, -1} << LV{A, -delta, 0.0, 0.0, 0};
    }

    const double kSg[2] = {1.0, -1.0};
    Loop hs[2], ss[2], ts[2];
    for (int k = 0; k < 2; ++k) {
        hs[k] = makeLoop(housing, kSg[k], f);
        ss[k] = makeLoop(shaft, kSg[k], f);
        if (hasSeat) ts[k] = makeLoop(seat, kSg[k], f);
    }
    for (int k = 0; k < 2; ++k) {
        g.sections.append(hs[k].sec);
        g.sections.append(ss[k].sec);
        if (hasSeat) g.sections.append(ts[k].sec);
    }

    // 볼
    for (int k = 0; k < 2; ++k) {
        ThrustBall b;
        b.center = f.at(kSg[k] * yc, H * 0.5);
        b.radius = rb;
        g.balls.append(b);
    }

    // 전체 그리기: 축 구멍 건너 면선 (BE3.LSP 의 연결선). 양 끝은 필렛 접점.
    if (p.drawFull) {
        // 하우징 와셔 윗면 (꼭짓점 5 = (A,t))
        g.lines.append(QLineF(hs[0].tanPrev.at(5), hs[1].tanPrev.at(5)));
        // 축 와셔 아랫면 (꼭짓점 0 = (A2,H-t)) 과 윗면 (꼭짓점 5 = (A2,H))
        g.lines.append(QLineF(ss[0].tanNext.at(0), ss[1].tanNext.at(0)));
        g.lines.append(QLineF(ss[0].tanPrev.at(5), ss[1].tanPrev.at(5)));
        if (!spherical) {
            // 평면자리형 바닥 면 (꼭짓점 0 = (A,0))
            g.lines.append(QLineF(hs[0].tanNext.at(0), hs[1].tanNext.at(0)));
        } else {
            // 구면 바닥의 안쪽 모서리 (꼭짓점 0 = (A,-δ))
            g.lines.append(QLineF(hs[0].tanNext.at(0), hs[1].tanNext.at(0)));
            if (hasSeat) {
                // 조심자리 와셔 바닥 면 (꼭짓점 0 = (A,hSeat))
                g.lines.append(QLineF(ts[0].tanNext.at(0), ts[1].tanNext.at(0)));
            }
        }
    }

    if (p.drawCenter) {
        // 축선 (위아래로 0.1H 연장) + 볼 중심선 (십자, 길이 ±0.3H)
        g.centerLines.append(QLineF(f.at(0.0, hBot - 0.1 * H), f.at(0.0, H + 0.1 * H)));
        for (int k = 0; k < 2; ++k) {
            const double cx = kSg[k] * yc;
            g.centerLines.append(QLineF(f.at(cx - 0.3 * H, H * 0.5), f.at(cx + 0.3 * H, H * 0.5)));
            g.centerLines.append(QLineF(f.at(cx, H * 0.5 - 0.3 * H), f.at(cx, H * 0.5 + 0.3 * H)));
        }
    }

    if (p.drawDims) {
        const double gap = qMax(p.D * 0.12, 5.0);
        // 위쪽: 축 와셔 안지름 d
        MyDimension dd = makeHorizontalDim(f.at(-A2, H), f.at(A2, H), f.at(0.0, H + gap).y());
        dd.prefix = QStringLiteral("Ø");
        g.dims.append(dd);
        // 아래쪽: 하우징 와셔 안지름 de, 바깥지름 D
        MyDimension dde = makeHorizontalDim(f.at(-A, hBot), f.at(A, hBot), f.at(0.0, hBot - gap).y());
        dde.prefix = QStringLiteral("Ø");
        g.dims.append(dde);
        const double hRim = spherical && !hasSeat ? 0.0 : hBot;
        MyDimension dD = makeHorizontalDim(f.at(-B, hRim), f.at(B, hRim),
                                           f.at(0.0, hBot - gap * 1.9).y());
        dD.prefix = QStringLiteral("Ø");
        g.dims.append(dD);
        // 오른쪽: 전체 높이 (H / H1 / H2)
        const double hLow = (spherical && !hasSeat) ? -delta : hBot;
        const double xLow = (spherical && !hasSeat) ? A : B;
        g.dims.append(makeVerticalDim(f.at(B, H), f.at(xLow, hLow), f.at(B + gap, 0.0).x()));
    }
    return g;
}

QPainterPath ThrustBearingTool::buildPath(const ThrustBearingParams &p, const QPointF &o, double angleDeg) {
    QPainterPath path;
    ThrustBearingGeometry g = build(p, o);
    rotateGeometry(g, o, angleDeg);
    if (g.sections.isEmpty()) return path;
    for (int i = 0; i < g.sections.size(); ++i) path.addPath(g.sections.at(i).hatchPath);
    for (int i = 0; i < g.balls.size(); ++i)
        path.addEllipse(g.balls.at(i).center, g.balls.at(i).radius, g.balls.at(i).radius);
    for (int i = 0; i < g.lines.size(); ++i) {
        path.moveTo(g.lines.at(i).p1());
        path.lineTo(g.lines.at(i).p2());
    }
    for (int i = 0; i < g.centerLines.size(); ++i) {
        path.moveTo(g.centerLines.at(i).p1());
        path.lineTo(g.centerLines.at(i).p2());
    }
    return path;
}

void ThrustBearingTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);

    ToolDefaults defaults(kToolNamespace, scene);
    ThrustBearingParams saved;
    saved.name       = defaults.getString(QStringLiteral("Name"), saved.name);
    saved.type       = defaults.getInt("Type", saved.type);
    saved.d          = defaults.getDouble("D_in",  saved.d);
    saved.de         = defaults.getDouble("D_housing", saved.de);
    saved.D          = defaults.getDouble("D_out", saved.D);
    saved.H          = defaults.getDouble("Height", saved.H);
    saved.c          = defaults.getDouble("CornerR", saved.c);
    saved.H1         = defaults.getDouble("Height1", saved.H1);
    saved.H2         = defaults.getDouble("Height2", saved.H2);
    saved.dbl        = defaults.getBool("Double", saved.dbl);
    saved.di         = defaults.getDouble("D_mid", saved.di);
    saved.a          = defaults.getDouble("MidThick", saved.a);
    saved.c1         = defaults.getDouble("MidR", saved.c1);
    saved.drawDims   = defaults.getBool("DrawDims",   saved.drawDims);
    saved.drawCenter = defaults.getBool("DrawCenter", saved.drawCenter);
    saved.drawHatch  = defaults.getBool("DrawHatch",  saved.drawHatch);
    saved.hatchScale = defaults.getDouble("HatchScale", 0.0);
    saved.drawFull   = defaults.getBool("DrawFull", saved.drawFull);

    ThrustBearingDialog dlg(nullptr);
    dlg.setParams(saved);
    if (dlg.exec() != QDialog::Accepted) {
        // 취소: activate 시점엔 toolFinished 가 아직 연결 전이라 바로 emit 하면 무시된다.
        m_cancelled = true;
        QTimer::singleShot(0, this, &ThrustBearingTool::finishTool);
        return;
    }
    m_params = dlg.params();
    defaults.put(QStringLiteral("Name"), m_params.name);
    defaults.put("Type", m_params.type);
    defaults.put("D_in",  m_params.d);
    defaults.put("D_housing", m_params.de);
    defaults.put("D_out", m_params.D);
    defaults.put("Height", m_params.H);
    defaults.put("CornerR", m_params.c);
    defaults.put("Height1", m_params.H1);
    defaults.put("Height2", m_params.H2);
    defaults.put("Double", m_params.dbl);
    defaults.put("D_mid", m_params.di);
    defaults.put("MidThick", m_params.a);
    defaults.put("MidR", m_params.c1);
    defaults.put("DrawDims",   m_params.drawDims);
    defaults.put("DrawCenter", m_params.drawCenter);
    defaults.put("DrawHatch",  m_params.drawHatch);
    defaults.put("HatchScale", m_params.hatchScale);
    defaults.put("DrawFull",   m_params.drawFull);

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->logMessage(
            tr("스러스트베어링(%7) %1 %2 (d=%3, de=%4, D=%5, H=%6)")
                .arg(ThrustBearingTable::displayName(m_params.name, m_params.type))
                .arg(ThrustBearingTable::typeLabel(m_params.type))
                .arg(m_params.d).arg(m_params.de).arg(m_params.D).arg(m_params.H)
                .arg(m_params.dbl ? tr("복식") : tr("단식")),
            CommandDock::Command);
    }
    showPositionPrompt();
}

void ThrustBearingTool::deactivate() {
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    AbstractTool::deactivate();
}

void ThrustBearingTool::showPositionPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 베어링 중심 위치 지정 (Enter=종료): "), {});
    }
}

void ThrustBearingTool::finishTool() {
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    emit toolFinished();
}

void ThrustBearingTool::ensureLayer(IAppContext *ctx, const QString &name, const QColor &color,
                                    const QString &linetype) {
    if (!ctx || !ctx->layerManager() || name.isEmpty()) return;
    if (ctx->layerManager()->getLayer(name)) return;
    Layer layer(name, color, 0.25, linetype, false, true, true, false);
    ctx->layerManager()->addLayer(layer);
}

QGraphicsItem *ThrustBearingTool::finalizeItem(QGraphicsItem *item, const QString &layer) {
    if (!item) return nullptr;
    IAppContext *ctx = getContext();
    if (CadItem *cad = dynamic_cast<CadItem *>(item)) cad->setLayerName(layer);
    item->setParentItem(m_scene->getLayerRoot(layer));
    if (item->scene() == m_scene) m_scene->removeItem(item);
    if (ctx && ctx->undoStack()) ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
    else                         m_scene->addItem(item);
    return item;
}

void ThrustBearingTool::placeAt(const QPointF &origin) {
    IAppContext *ctx = getContext();
    if (m_cancelled) return;
    if (!m_scene || !ctx || !ctx->entityFactory() || !ctx->layerManager()) return;

    ThrustBearingGeometry g = build(m_params, origin);
    rotateGeometry(g, origin, m_angleDeg);
    if (g.sections.isEmpty()) return;

    const QString bodyLayer = BearingLayer::bodyLayer(m_scene);
    ensureLayer(ctx, bodyLayer, QColor(255, 255, 255), QStringLiteral("Continuous"));

    QString centerLayer = DrawingSettingsCommand::s_centerLineLayer;
    if (centerLayer.isEmpty() || !ctx->layerManager()->getLayer(centerLayer)) {
        centerLayer = QString::fromLatin1(kLayerCenter);
        ensureLayer(ctx, centerLayer, QColor(255, 0, 0), QStringLiteral("CENTER"));
    }
    QString dimLayer = m_scene->dimensionStyle().dimLineLayer;
    if (dimLayer.isEmpty() || !ctx->layerManager()->getLayer(dimLayer)) {
        dimLayer = QString::fromLatin1(kLayerDim);
        ensureLayer(ctx, dimLayer, QColor(0, 255, 255), QStringLiteral("Continuous"));
    }

    // 해칭 레이어: 해칭 툴 설정 → 현재 레이어 → 외형 레이어
    QString hatchLayer = ToolDefaults("Hatch", m_scene).getString(QStringLiteral("Layer"), QString());
    if (hatchLayer.isEmpty() || !ctx->layerManager()->getLayer(hatchLayer)) {
        Layer *cur = ctx->layerManager()->getCurrentLayer();
        hatchLayer = cur ? cur->name() : bodyLayer;
    }

    const QPen bodyPen   = AbstractTool::layerPenFor(ctx, bodyLayer);
    const QPen centerPen = AbstractTool::layerPenFor(ctx, centerLayer);
    const QPen dimPen    = AbstractTool::layerPenFor(ctx, dimLayer);
    const QPen hatchPen  = AbstractTool::layerPenFor(ctx, hatchLayer);
    EntityFactory *ef = ctx->entityFactory();

    const double hatchScale = m_params.hatchScale > 0.0
        ? m_params.hatchScale
        : ThrustBearingTable::autoHatchScale(m_params.H, m_params.dbl ? m_params.a : -1.0);

    QUndoStack *undo = ctx->undoStack();
    if (undo) undo->beginMacro(tr("스러스트베어링"));

    for (int i = 0; i < g.sections.size(); ++i) {
        const ThrustSection &s = g.sections.at(i);
        finalizeItem(ef->createPolyline(s.outline, true, bodyPen, s.bulges), bodyLayer);
        if (m_params.drawHatch)
            finalizeItem(ef->createHatch(s.hatchPath, QStringLiteral("ANSI31"), hatchScale, 0.0, hatchPen),
                         hatchLayer);
    }
    for (int i = 0; i < g.balls.size(); ++i)
        finalizeItem(ef->createCircle(g.balls.at(i).center, g.balls.at(i).radius, bodyPen), bodyLayer);
    for (int i = 0; i < g.lines.size(); ++i)
        finalizeItem(ef->createLine(g.lines.at(i), bodyPen), bodyLayer);
    for (int i = 0; i < g.centerLines.size(); ++i)
        finalizeItem(ef->createLine(g.centerLines.at(i), centerPen), centerLayer);
    for (int i = 0; i < g.dims.size(); ++i)
        finalizeItem(ef->createDimension(g.dims.at(i), dimPen), dimLayer);

    if (undo) undo->endMacro();

    if (ctx->commandDock()) {
        ctx->commandDock()->logMessage(
            tr("스러스트베어링 %1 생성 완료.")
                .arg(ThrustBearingTable::displayName(m_params.name, m_params.type)),
            CommandDock::Info);
    }
    showPositionPrompt();
}

void ThrustBearingTool::startAngleStep(const QPointF &origin) {
    m_baseOrigin = origin;
    m_awaitAngle = true;
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 회전 각도 지정 (클릭=방향, 각도 입력, Enter=0°, F8=직교, Esc=취소): "), {});
    }
}

void ThrustBearingTool::finishAngleStep(double angleDeg) {
    if (!m_awaitAngle) return;
    m_awaitAngle = false;
    m_angleDeg = angleDeg;
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    placeAt(m_baseOrigin);
    m_angleDeg = 0.0;
}

void ThrustBearingTool::cancelAngleStep() {
    m_awaitAngle = false;
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    showPositionPrompt();
}

double ThrustBearingTool::angleFromPoint(const QPointF &p) const {
    const QPointF d = p - m_baseOrigin;
    if (qAbs(d.x()) < 1e-9 && qAbs(d.y()) < 1e-9) return 0.0;
    return qRadiansToDegrees(qAtan2(d.y(), d.x()));
}

bool ThrustBearingTool::processCommand(const QString &command) {
    if (m_cancelled) return true;
    const QString cmd = command.trimmed();
    IAppContext *ctx = getContext();
    if (!ctx) return false;

    if (m_awaitAngle) {
        if (cmd.isEmpty()) {
            finishAngleStep(0.0);
            return true;
        }
        bool ok = false;
        const double ang = cmd.toDouble(&ok);
        if (ok) {
            finishAngleStep(ang);
            return true;
        }
        if (ctx->parser()) {
            QPointF resultPos;
            const bool ortho = ctx->snapManager() && ctx->snapManager()->isOrtho();
            if (ctx->parser()->parseCoordinate(cmd, m_baseOrigin, m_baseOrigin, ortho, resultPos)) {
                finishAngleStep(angleFromPoint(resultPos));
                return true;
            }
        }
        return false;
    }

    if (cmd.isEmpty()) {
        finishTool();
        return true;
    }
    if (ctx->parser()) {
        QPointF resultPos;
        if (ctx->parser()->parseCoordinate(cmd, QPointF(0, 0), QPointF(0, 0), false, resultPos)) {
            startAngleStep(resultPos);
            return true;
        }
    }
    return false;
}

void ThrustBearingTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene || m_cancelled) return;
    if (button == Qt::RightButton) {
        if (m_awaitAngle) cancelAngleStep();
        else              finishTool();
        return;
    }
    if (button != Qt::LeftButton) return;

    IAppContext *ctx = getContext();
    QPointF snappedPos = pos;
    if (m_awaitAngle) {
        if (ctx && ctx->snapManager())
            snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, m_baseOrigin).snappedPos;
        finishAngleStep(angleFromPoint(snappedPos));
        return;
    }
    if (ctx && ctx->snapManager()) {
        snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, QPointF()).snappedPos;
    }
    startAngleStep(snappedPos);
}

void ThrustBearingTool::mouseMove(QPointF pos) {
    if (!m_scene || m_cancelled) return;

    IAppContext *ctx = getContext();
    QPointF snappedPos = pos;
    SnapType snapType = SnapType::None;
    if (m_awaitAngle) {
        // 기준점에서 커서 방향으로 회전한 모양을 미리 보여 준다 (F8 직교 적용).
        if (ctx && ctx->snapManager()) {
            const SnapManager::SnapResult res = ctx->snapManager()->snapPoint(pos, m_scene, m_baseOrigin);
            snappedPos = res.snappedPos;
            snapType = res.type;
        }
        m_scene->setSnapMarker(snappedPos, snapType);
        updateGhostPreview(buildPath(m_params, m_baseOrigin, angleFromPoint(snappedPos)),
                           currentLayerName());
        return;
    }
    if (ctx && ctx->snapManager()) {
        const SnapManager::SnapResult res = ctx->snapManager()->snapPoint(pos, m_scene, QPointF());
        snappedPos = res.snappedPos;
        snapType = res.type;
    }
    m_scene->setSnapMarker(snappedPos, snapType);
    updateGhostPreview(buildPath(m_params, snappedPos), currentLayerName());
}

void ThrustBearingTool::keyPress(QKeyEvent *event) {
    if (m_cancelled) return;
    if (m_awaitAngle) {
        if (event->key() == Qt::Key_Escape) {
            cancelAngleStep();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
            event->key() == Qt::Key_Space) {
            finishAngleStep(0.0);
            return;
        }
        AbstractTool::keyPress(event);   // F8 등
        return;
    }
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return ||
        event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) {
        finishTool();
        return;
    }
    AbstractTool::keyPress(event);
}
