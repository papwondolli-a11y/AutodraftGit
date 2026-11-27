#include "plateview.h"

#include "holedata.h"
#include "holeitem.h"
#include "mechpartdata.h"
#include "mygraphicsscene.h"
#include "scenesymbolregistry.h"

#include <QGraphicsItem>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtMath>
#include <QVector3D>
#include <algorithm>
#include <cmath>

namespace {

QPainterPath ringsPath(const QList<QPolygonF> &rings)
{
    QPainterPath p;
    p.setFillRule(Qt::OddEvenFill);
    for (int i = 0; i < rings.size(); ++i) {
        QPolygonF pg = rings.at(i);
        if (!pg.isEmpty())
            pg << pg.first();
        p.addPolygon(pg);
    }
    return p;
}

QRectF ringsBox(const QList<QPolygonF> &rings)
{
    QRectF b;
    for (int i = 0; i < rings.size(); ++i)
        b = b.isNull() ? rings.at(i).boundingRect() : b.united(rings.at(i).boundingRect());
    return b;
}

// 뷰 축: n = 판에서 뷰 쪽 바깥 법선(보는 방향 = −n), u = n 을 반시계 90°, c = 뷰 기준점
//   판 점 P(도면 2D), 두께 Z → 뷰 점 = c + u·((P − a0)·u) − n·Z  (좌·우·평면도도 이 식의 특수한 경우)
struct ViewAxes {
    QPointF c;
    QPointF n;
    QPointF u;
};

bool viewAxes(int kind, const QPointF &a0, double param, ViewAxes &ax)
{
    if (ProjectionView::isFaceKind(kind)) {
        const double fa = qDegreesToRadians(ProjectionView::faceAngle(kind));
        ax.n = QPointF(std::cos(fa), std::sin(fa));
    } else if (kind == ProjectionView::Right) {
        ax.n = QPointF(1.0, 0.0);
    } else if (kind == ProjectionView::Left) {
        ax.n = QPointF(-1.0, 0.0);
    } else if (kind == ProjectionView::Top) {
        ax.n = QPointF(0.0, 1.0);
    } else {
        return false;
    }
    ax.u = QPointF(-ax.n.y(), ax.n.x());
    ax.c = ProjectionView::anchorFor(kind, a0, 0.0, param);
    return true;
}

double dot2(const QPointF &a, const QPointF &b)
{
    return a.x() * b.x() + a.y() * b.y();
}

// 뷰 좌표 (s = u 방향, Z = 두께) → 도면
QPointF viewPt(const ViewAxes &ax, double s, double Z)
{
    return ax.c + ax.u * s - ax.n * Z;
}

// 판 위 직선 {P : (P − a0)·u = s} 와 판 경계가 만나는 곳의 법선 방향 좌표 w = (P − a0)·n (정렬, 큰 쪽 = 보는 쪽 앞)
QList<double> crossingsW(const QList<QPolygonF> &rings, const QPointF &a0, const ViewAxes &ax, double s)
{
    QList<double> ws;
    for (int r = 0; r < rings.size(); ++r) {
        const QPolygonF &p = rings.at(r);
        for (int k = 0; k < p.size(); ++k) {
            const QPointF a = p.at(k) - a0, b = p.at((k + 1) % p.size()) - a0;
            const double sa = dot2(a, ax.u), sb = dot2(b, ax.u);
            if ((sa > s) == (sb > s))
                continue;
            const double t = (s - sa) / (sb - sa);
            ws << dot2(a, ax.n) + t * (dot2(b, ax.n) - dot2(a, ax.n));
        }
    }
    std::sort(ws.begin(), ws.end());
    return ws;
}

// 판 꼭짓점들의 뷰 좌표 s 범위
void sRange(const QList<QPolygonF> &rings, const QPointF &a0, const ViewAxes &ax, double &smin, double &smax)
{
    smin = 1e300;
    smax = -1e300;
    for (int r = 0; r < rings.size(); ++r)
        for (int k = 0; k < rings.at(r).size(); ++k) {
            const double sv = dot2(rings.at(r).at(k) - a0, ax.u);
            smin = std::min(smin, sv);
            smax = std::max(smax, sv);
        }
}

// 판 돌기를 합칠 때 판 속으로 넣는 길이 (Blender Union 이 맞닿은 면에서 흔들리지 않게). 투상에서는 다시 뺀다
const double kBossOverlap = 0.05;

// ── 돌기·판의 3D 입체 (투상도 가림 판정용) ──
//   입체 = 단면 고리(e1, e2 평면) × 축(ax) 방향 t0~t1. 판 = (X, Y) × Z, 돌기 = (s, Z) × 법선 w
struct Solid {
    QVector3D o;
    QVector3D ax;
    QVector3D e1;
    QVector3D e2;
    QList<QPolygonF> rings;
    double t0 = 0.0;
    double t1 = 0.0;
    bool merge = true;
    bool cut = false;      // 파내기 (재료에서 뺌)
};

Solid plateSolid(const ExtrudeInfo &ei)
{
    Solid s;
    s.o = QVector3D(0.0f, 0.0f, 0.0f);
    s.ax = QVector3D(0.0f, 0.0f, 1.0f);
    s.e1 = QVector3D(1.0f, 0.0f, 0.0f);
    s.e2 = QVector3D(0.0f, 1.0f, 0.0f);
    s.rings = ei.rings;
    s.t0 = ei.x0;
    s.t1 = ei.x1;
    return s;
}

Solid bossSolid(const ExtrudeInfo &b)
{
    Solid s;
    if (b.plateFront) {
        // 정면 파내기: 도면 (X, Y) × Z
        s.o = QVector3D(0.0f, 0.0f, 0.0f);
        s.ax = QVector3D(0.0f, 0.0f, 1.0f);
        s.e1 = QVector3D(1.0f, 0.0f, 0.0f);
        s.e2 = QVector3D(0.0f, 1.0f, 0.0f);
    } else {
        const double a = qDegreesToRadians(b.angleDeg);
        s.o = QVector3D(float(b.a0.x()), float(b.a0.y()), 0.0f);
        s.ax = QVector3D(float(std::cos(a)), float(std::sin(a)), 0.0f);
        s.e1 = QVector3D(float(-std::sin(a)), float(std::cos(a)), 0.0f);
        s.e2 = QVector3D(0.0f, 0.0f, 1.0f);
    }
    s.rings = b.rings;
    s.t0 = b.x0 + ((b.merge && !b.cut) ? kBossOverlap : 0.0);   // 투상은 실제 판 표면부터 (합치기 겹침 빼고)
    s.t1 = b.x1;
    s.merge = b.merge || b.cut;
    s.cut = b.cut;
    return s;
}

QVector3D solidPt(const Solid &s, double t, const QPointF &p)
{
    return s.o + s.ax * float(t) + s.e1 * float(p.x()) + s.e2 * float(p.y());
}

bool ringsContain(const QList<QPolygonF> &rings, const QPointF &q)
{
    bool in = false;
    for (int r = 0; r < rings.size(); ++r)
        if (rings.at(r).containsPoint(q, Qt::OddEvenFill))
            in = !in;
    return in;
}

double ringsDist(const QList<QPolygonF> &rings, const QPointF &q)
{
    double best = 1e300;
    for (int r = 0; r < rings.size(); ++r) {
        const QPolygonF &p = rings.at(r);
        for (int k = 0; k < p.size(); ++k) {
            const QPointF a = p.at(k), b = p.at((k + 1) % p.size());
            const QPointF d = b - a;
            const double l2 = dot2(d, d);
            const double t = l2 > 1e-18 ? std::max(0.0, std::min(1.0, dot2(q - a, d) / l2)) : 0.0;
            const QPointF c = a + d * t;
            best = std::min(best, std::hypot(q.x() - c.x(), q.y() - c.y()));
        }
    }
    return best;
}

// 점이 입체 속(경계에서 tol 이상 안쪽)인가
bool insideStrict(const Solid &s, const QVector3D &P, double tol)
{
    const QVector3D d = P - s.o;
    const double t = QVector3D::dotProduct(d, s.ax);
    if (t <= s.t0 + tol || t >= s.t1 - tol)
        return false;
    const QPointF q(QVector3D::dotProduct(d, s.e1), QVector3D::dotProduct(d, s.e2));
    return ringsContain(s.rings, q) && ringsDist(s.rings, q) > tol;
}

// P 에서 V 방향(보는 사람 쪽) 시선이 입체 속(경계에서 tol 이상 안쪽)을 지나는가 — 면을 스치기만 하면 가리지 않음
bool rayHits(const Solid &s, const QVector3D &P, const QVector3D &V, double tol)
{
    const QVector3D d = P - s.o;
    const double tP = QVector3D::dotProduct(d, s.ax), vt = QVector3D::dotProduct(V, s.ax);
    double la = tol, lb = 1e7;
    if (std::fabs(vt) < 1e-12) {
        if (tP <= s.t0 + tol || tP >= s.t1 - tol)
            return false;
    } else {
        double a = (s.t0 + tol - tP) / vt, b = (s.t1 - tol - tP) / vt;
        if (a > b)
            std::swap(a, b);
        la = std::max(la, a);
        lb = std::min(lb, b);
    }
    if (la >= lb)
        return false;
    const QPointF p0(QVector3D::dotProduct(d, s.e1), QVector3D::dotProduct(d, s.e2));
    const QPointF vp(QVector3D::dotProduct(V, s.e1), QVector3D::dotProduct(V, s.e2));
    const QPointF qa = p0 + vp * la, qb = p0 + vp * lb;
    // 2D 선분 qa→qb 와 고리 변의 교차 매개변수로 구간을 나눠 가운데 점 검사
    QList<double> mu;
    mu << 0.0 << 1.0;
    const QPointF sd = qb - qa;
    if (dot2(sd, sd) > 1e-18) {
        for (int r = 0; r < s.rings.size(); ++r) {
            const QPolygonF &p = s.rings.at(r);
            for (int k = 0; k < p.size(); ++k) {
                const QPointF a = p.at(k), e = p.at((k + 1) % p.size()) - a;
                const double den = sd.x() * e.y() - sd.y() * e.x();
                if (std::fabs(den) < 1e-18)
                    continue;
                const QPointF w = a - qa;
                const double m = (w.x() * e.y() - w.y() * e.x()) / den;
                const double n = (w.x() * sd.y() - w.y() * sd.x()) / den;
                if (m > 0.0 && m < 1.0 && n >= 0.0 && n <= 1.0)
                    mu << m;
            }
        }
    }
    std::sort(mu.begin(), mu.end());
    for (int i = 0; i + 1 < mu.size(); ++i) {
        const QPointF q = qa + sd * ((mu.at(i) + mu.at(i + 1)) * 0.5);
        if (ringsContain(s.rings, q) && ringsDist(s.rings, q) > tol)
            return true;
    }
    return false;
}

// 입체 속인가 — m > 0 이면 경계에서 m 이상 안쪽만, m < 0 이면 경계에서 |m| 까지 넓혀서
bool insideMargin(const Solid &s, const QVector3D &P, double m)
{
    if (m >= 0.0)
        return insideStrict(s, P, m);
    const QVector3D d = P - s.o;
    const double t = QVector3D::dotProduct(d, s.ax);
    if (t < s.t0 + m || t > s.t1 - m)
        return false;
    const QPointF q(QVector3D::dotProduct(d, s.e1), QVector3D::dotProduct(d, s.e2));
    return ringsContain(s.rings, q) || ringsDist(s.rings, q) < -m;
}

// 재료 속 = 판·돌기 중 하나의 속(tol 안쪽)이고 어느 파내기에도(경계 포함) 들지 않음
bool inMaterial(const QList<Solid> &solids, const QVector3D &P, double tol)
{
    bool in = false;
    for (int j = 0; j < solids.size() && !in; ++j)
        if (!solids.at(j).cut && insideStrict(solids.at(j), P, tol))
            in = true;
    if (!in)
        return false;
    for (int j = 0; j < solids.size(); ++j)
        if (solids.at(j).cut && insideMargin(solids.at(j), P, -tol))
            return false;
    return true;
}

// P 에서 보는 쪽(V)으로 시선이 재료를 지나는가. 파내기가 없으면 입체별 해석식, 있으면 시선을 따라가며 검사
bool occluded(const QList<Solid> &solids, const QVector3D &P, const QVector3D &V, double tol, double reach)
{
    bool anyCut = false;
    for (int j = 0; j < solids.size() && !anyCut; ++j)
        anyCut = solids.at(j).cut;
    if (!anyCut) {
        for (int j = 0; j < solids.size(); ++j)
            if (rayHits(solids.at(j), P + V * float(tol), V, tol))
                return true;
        return false;
    }
    const int N = 600;
    const double step = reach / N;
    for (int i = 1; i <= N; ++i)
        if (inMaterial(solids, P + V * float(tol * 2.0 + step * i), tol))
            return true;
    return false;
}

// 투상: 측면·보조 = ViewAxes, 정면 = (X, Y). toward = 보는 사람 쪽 방향
struct ViewProj {
    bool front = false;
    ViewAxes ax;
    QPointF a0;
    QVector3D toward;
    QPointF map(const QVector3D &P) const
    {
        if (front)
            return QPointF(P.x(), P.y());
        const QPointF q(P.x(), P.y());
        return viewPt(ax, dot2(q - a0, ax.u), P.z());
    }
};

// 3D 선분을 조각내 보임/숨음/버림 판정 후 투상해 넣는다.
//   self = 이 선분이 속한 입체 번호. 다른 입체 속 조각: 합치기면 버림(한 덩어리), 아니면 숨은선.
//   onPlateEdge = 정면에서 판 외곽선 위에 겹치는 조각은 버림(사용자 선이 있음)
void addClassified(ProjDrawing &out, const QVector3D &A, const QVector3D &B, const ViewProj &vp,
                   const QList<Solid> &solids, int self, double tol, const QList<QPolygonF> *dropOn, double reach)
{
    const QPointF a2 = vp.map(A), b2 = vp.map(B);
    if (QLineF(a2, b2).length() < tol)
        return;
    const int K = 24;
    int status[K];
    for (int k = 0; k < K; ++k) {
        const QVector3D M = A + (B - A) * float((k + 0.5) / K);
        int st = 1;
        if (solids.at(self).cut) {
            // 파낸 모서리: 재료(판·돌기) 속이나 겉에 있어야 진짜 모서리, 다른 파내기 속이면 없음
            bool onMat = false;
            for (int j = 0; j < solids.size() && !onMat; ++j)
                if (j != self && !solids.at(j).cut && insideMargin(solids.at(j), M, -tol))
                    onMat = true;
            if (!onMat)
                st = 0;
            for (int j = 0; j < solids.size() && st == 1; ++j)
                if (j != self && solids.at(j).cut && insideStrict(solids.at(j), M, tol))
                    st = 0;
        } else {
            for (int j = 0; j < solids.size() && st == 1; ++j) {
                if (j == self)
                    continue;
                if (solids.at(j).cut) {
                    if (insideStrict(solids.at(j), M, tol))
                        st = 0;                         // 파낸 자리
                } else if (insideStrict(solids.at(j), M, tol)) {
                    st = solids.at(self).merge ? 0 : 2;
                }
            }
        }
        if (st == 1 && dropOn && ringsDist(*dropOn, QPointF(M.x(), M.y())) < tol)
            st = 0;
        if (st == 1 && occluded(solids, M, vp.toward, tol, reach))
            st = 2;
        status[k] = st;
    }
    int k0 = 0;
    while (k0 < K) {
        int k1 = k0;
        while (k1 + 1 < K && status[k1 + 1] == status[k0])
            ++k1;
        if (status[k0] != 0) {
            const QVector3D P0 = A + (B - A) * float(double(k0) / K), P1 = A + (B - A) * float(double(k1 + 1) / K);
            const QLineF ln(vp.map(P0), vp.map(P1));
            if (ln.length() > tol)
                (status[k0] == 1 ? out.visible : out.hidden) << ln;
        }
        k0 = k1 + 1;
    }
}

// 돌기 모서리: 양 끝 뚜껑 고리 변 + 옆 모서리(꺾인 곳·이 뷰에서 윤곽인 곳)
void addBossEdges(ProjDrawing &out, const QList<Solid> &solids, int idx, const ViewProj &vp, double tol,
                  const QList<QPolygonF> *dropOn, double reach)
{
    const Solid &s = solids.at(idx);
    const double creaseCos = std::cos(qDegreesToRadians(20.0));
    for (int r = 0; r < s.rings.size(); ++r) {
        const QPolygonF &p = s.rings.at(r);
        const int nv = p.size();
        if (nv < 3)
            continue;
        for (int k = 0; k < nv; ++k) {
            const QPointF a = p.at(k), b = p.at((k + 1) % nv);
            addClassified(out, solidPt(s, s.t0, a), solidPt(s, s.t0, b), vp, solids, idx, tol, dropOn, reach);
            addClassified(out, solidPt(s, s.t1, a), solidPt(s, s.t1, b), vp, solids, idx, tol, dropOn, reach);
        }
        // 옆 모서리 방향이 뷰에서 점으로 줄면(그 면을 정면으로 보는 뷰) 옆 모서리 없음
        const QPointF L = vp.map(solidPt(s, s.t1, p.at(0))) - vp.map(solidPt(s, s.t0, p.at(0)));
        const double Ll = std::hypot(L.x(), L.y());
        if (Ll < tol)
            continue;
        const QPointF Lh = L / Ll;
        for (int k = 0; k < nv; ++k) {
            const QPointF prev = p.at((k + nv - 1) % nv), cur = p.at(k), next = p.at((k + 1) % nv);
            const QPointF d1 = cur - prev, d2 = next - cur;
            const double l1 = std::hypot(d1.x(), d1.y()), l2 = std::hypot(d2.x(), d2.y());
            if (l1 < 1e-9 || l2 < 1e-9)
                continue;
            const bool crease = dot2(d1, d2) / (l1 * l2) < creaseCos;
            const QPointF qp = vp.map(solidPt(s, s.t0, prev)), qc = vp.map(solidPt(s, s.t0, cur)),
                          qn = vp.map(solidPt(s, s.t0, next));
            const double cp = Lh.x() * qp.y() - Lh.y() * qp.x(), cc = Lh.x() * qc.y() - Lh.y() * qc.x(),
                         cn = Lh.x() * qn.y() - Lh.y() * qn.x();
            const bool extreme = (cp - cc) * (cn - cc) > 0.0;
            if (crease || extreme)
                addClassified(out, solidPt(s, s.t0, cur), solidPt(s, s.t1, cur), vp, solids, idx, tol, dropOn, reach);
        }
    }
}

// 외형 끝에서 살짝 안쪽 (교차 계산이 꼭짓점에 걸리지 않게)
double ds2(double size)
{
    return std::max(1e-9, size * 1e-6);
}

double ringSigned(const QPolygonF &p)
{
    double a = 0.0;
    for (int i = 0; i < p.size(); ++i) {
        const QPointF &u = p.at(i), &v = p.at((i + 1) % p.size());
        a += u.x() * v.y() - v.x() * u.y();
    }
    return a * 0.5;
}


void addUnique(QList<double> &list, double v)
{
    for (int i = 0; i < list.size(); ++i)
        if (std::fabs(list.at(i) - v) < 1e-6)
            return;
    list << v;
}

// 구멍이 그 방향으로 차지하는 길이 (관통 = 두께, 막힘 = 드릴 깊이)
double holeLength(const PlateHole &h)
{
    if (h.hole.throughHole)
        return h.thickness;
    HoleFeature tmp;
    tmp.hole = h.hole;
    return std::min(tmp.drillDepth(h.thickness), h.thickness);
}

} // namespace

bool PlateView::isPlate(MyGraphicsScene *scene, const QString &uuid)
{
    if (!scene || !scene->symbolRegistry()->hasSymbol(uuid))
        return false;
    const SymbolInfo info = scene->symbolRegistry()->symbolInfo(uuid);
    return info.type == QLatin1String("MechExtrude") && info.props.value(QStringLiteral("Standalone")).toBool();
}

bool PlateView::frame(MyGraphicsScene *scene, const QString &uuid, ExtrudeInfo &ei, QPointF &a0, QPointF &ref)
{
    if (!isPlate(scene, uuid) || !ExtrudeFeature::resolve(scene, uuid, ei) || !ei.standalone)
        return false;
    const QRectF b = ringsBox(ei.rings);
    if (b.isNull())
        return false;
    a0 = b.center();
    ref = b.topLeft();
    return true;
}

QList<PlateView::FaceView> PlateView::views(MyGraphicsScene *scene, const QString &uuid)
{
    QList<FaceView> out;
    ExtrudeInfo ei;
    QPointF a0, ref;
    if (!frame(scene, uuid, ei, a0, ref))
        return out;
    const QJsonArray views =
        QJsonDocument::fromJson(scene->symbolRegistry()->symbolInfo(uuid).props.value(QStringLiteral("Views")).toString().toUtf8())
            .array();
    for (int i = 0; i < views.size(); ++i) {
        const QJsonObject v = views.at(i).toObject();
        const int kind = v.value(QStringLiteral("kind")).toInt();
        if (kind != ProjectionView::Left && kind != ProjectionView::Right && kind != ProjectionView::Top &&
            !ProjectionView::isFaceKind(kind))
            continue;
        double param = v.value(QStringLiteral("param")).toDouble();
        if (v.contains(QStringLiteral("ax"))) {
            const QPointF anc(v.value(QStringLiteral("ax")).toDouble() + ref.x() - v.value(QStringLiteral("rx")).toDouble(),
                              v.value(QStringLiteral("ay")).toDouble() + ref.y() - v.value(QStringLiteral("ry")).toDouble());
            param = ProjectionView::paramFor(kind, a0, 0.0, anc);
        }
        FaceView fv;
        fv.viewId = v.value(QStringLiteral("id")).toString();
        fv.kind = kind;
        ViewAxes ax;
        if (!viewAxes(kind, a0, param, ax))
            continue;
        fv.center = ax.c;
        fv.n = ax.n;
        fv.u = ax.u;
        // 뷰가 차지하는 (기울어질 수 있는) 사각: s 범위 × 두께
        double smin = 0.0, smax = 0.0;
        sRange(ei.rings, a0, ax, smin, smax);
        fv.poly << viewPt(ax, smin, ei.x0) << viewPt(ax, smax, ei.x0) << viewPt(ax, smax, ei.x1) << viewPt(ax, smin, ei.x1);
        out << fv;
    }
    return out;
}

QList<PlateHole> PlateView::holes(MyGraphicsScene *scene, const QString &uuid)
{
    QList<PlateHole> out;
    ExtrudeInfo ei;
    QPointF a0, ref;
    if (!frame(scene, uuid, ei, a0, ref))
        return out;
    const QPainterPath plate = ringsPath(ei.rings);
    const double z0 = ei.x0, z1 = ei.x1;
    const QList<FaceView> fvs = views(scene, uuid);
    const QList<QGraphicsItem *> items = scene->items();

    // 구멍R·구멍P — 정면(판 공간 안) 또는 측면·평면도 안
    QHash<QString, MyHole> specs;
    for (int i = 0; i < items.size(); ++i) {
        const HoleItem *hi = dynamic_cast<const HoleItem *>(items.at(i));
        // 숨긴 투상도 안이라 화면에서만 숨긴 구멍은 계속 판 구멍으로 셈
        if (!hi || (!hi->isVisible() && !hi->data(MECH_CONCEALED_HOLE).toBool()))
            continue;
        const MyHole &mh = hi->holeData();
        const QString gid = hi->data(HOLE_DATA_GROUP_ID).toString();
        if (!gid.isEmpty() && !specs.contains(gid))
            specs.insert(gid, mh);
        const QPointF p = hi->sceneTransform().map(QPointF(mh.position.x, mh.position.y));
        bool placed = false;
        for (int v = 0; v < fvs.size() && !placed; ++v) {
            const FaceView &fv = fvs.at(v);
            if (!fv.poly.containsPoint(p, Qt::OddEvenFill))
                continue;
            // 그 면에서 판 안쪽(−n)으로: 입구 = 그 직선에서 보는 쪽 가장 앞 경계, 두께 = 그 재료 구간
            ViewAxes ax;
            ax.c = fv.center;
            ax.n = fv.n;
            ax.u = fv.u;
            const QPointF d = p - fv.center;
            const double sv = dot2(d, ax.u), Z = -dot2(d, ax.n);
            const QList<double> ws = crossingsW(ei.rings, a0, ax, sv);
            if (ws.size() < 2 || Z <= z0 || Z >= z1)
                continue;
            const QPointF e2 = a0 + ax.u * sv + ax.n * ws.last();
            PlateHole h;
            h.hole = mh;
            h.viewId = fv.viewId;
            h.entry = QVector3D(float(e2.x()), float(e2.y()), float(Z));
            h.dir = QVector3D(float(-ax.n.x()), float(-ax.n.y()), 0.0f);
            h.thickness = ws.last() - ws.at(ws.size() - 2);
            h.item = hi;
            out << h;
            placed = true;
        }
        if (placed || !plate.contains(p))
            continue;
        PlateHole h;                                    // 정면(앞면)에서 −Z
        h.hole = mh;
        h.entry = QVector3D(float(p.x()), float(p.y()), float(z1));
        h.dir = QVector3D(0.0f, 0.0f, -1.0f);
        h.thickness = z1 - z0;
        out << h;
    }

    // 정면에 구멍 단면 도구로 그린 옆면 구멍 (방향이 도면 안) → Z = 두께 가운데
    QSet<QString> seen;
    for (int i = 0; i < items.size(); ++i) {
        const QGraphicsItem *gi = items.at(i);
        const QString gid = gi->data(HOLE_SECTION_GROUP_ID).toString();
        if (gid.isEmpty() || !gi->data(MECH_VIEW_ID).toString().isEmpty() || !gi->isVisible() || !specs.contains(gid))
            continue;
        const QPointF top(gi->data(HOLE_SECTION_TOP_X).toDouble(), gi->data(HOLE_SECTION_TOP_Y).toDouble());
        QPointF dir(gi->data(HOLE_SECTION_DIR_X).toDouble(), gi->data(HOLE_SECTION_DIR_Y).toDouble());
        const double dl = std::hypot(dir.x(), dir.y());
        if (dl < 1e-9)
            continue;
        dir /= dl;
        const QString key = QStringLiteral("%1|%2|%3|%4|%5").arg(gid).arg(top.x(), 0, 'f', 4).arg(top.y(), 0, 'f', 4)
                                .arg(dir.x(), 0, 'f', 4).arg(dir.y(), 0, 'f', 4);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        const double thick = gi->data(HOLE_SECTION_MATERIAL_THICKNESS).toDouble();
        const QPointF probe = top + dir * std::max(0.05, std::min(0.5, thick * 0.25));
        if (!plate.contains(probe))
            continue;                                   // 이 판의 재료 안으로 들어가는 구멍만
        // 재료 두께 = 입구에서 구멍 방향으로 다음 판 경계까지 (구멍 단면 태그의 두께는 막힌 탭이면 0·탭 깊이라 쓰지 않음)
        ViewAxes dax;
        dax.n = dir;
        dax.u = QPointF(-dir.y(), dir.x());
        const double wTop = dot2(top - a0, dir);
        const QList<double> ws = crossingsW(ei.rings, a0, dax, dot2(top - a0, dax.u));
        double matLen = 0.0;
        for (int k = 0; k < ws.size() && matLen <= 0.0; ++k)
            if (ws.at(k) > wTop + 1e-6)
                matLen = ws.at(k) - wTop;
        if (matLen <= 0.0)
            matLen = thick;
        if (matLen <= 0.0)
            continue;
        PlateHole h;
        h.hole = specs.value(gid);
        h.entry = QVector3D(float(top.x()), float(top.y()), float((z0 + z1) * 0.5));
        h.dir = QVector3D(float(dir.x()), float(dir.y()), 0.0f);
        h.thickness = matLen;
        h.fromSection = true;
        out << h;
    }
    return out;
}

ProjDrawing PlateView::build(const ExtrudeInfo &ei, int kind, const QPointF &a0, double param, bool showHidden,
                             const QList<PlateHole> &holes, const QString &viewId, const QList<ExtrudeInfo> &bosses)
{
    ProjDrawing out;
    if (ei.rings.isEmpty())
        return out;
    const QRectF b = ringsBox(ei.rings);
    const double z0 = ei.x0, z1 = ei.x1;
    const double size = std::max(b.width(), b.height());
    const double eps = std::max(1e-9, size * 1e-6);

    // 가림 판정용 입체: 0 = 판, 1.. = 돌기
    QList<Solid> solids;
    solids << plateSolid(ei);
    for (int i = 0; i < bosses.size(); ++i)
        solids << bossSolid(bosses.at(i));
    const double tol = std::max(1e-6, size * 1e-5);
    const double reach = 3.0 * (size + (z1 - z0));     // 가림 판정 시선 길이

    if (kind == ProjectionView::Front) {
        // 정면: 돌기 모서리 (판 외곽선과 겹치는 곳은 사용자 선이 있으므로 뺌)
        ViewProj fp;
        fp.front = true;
        fp.toward = QVector3D(0.0f, 0.0f, 1.0f);
        for (int i = 0; i < bosses.size(); ++i)
            if (!bosses.at(i).plateFront)               // 정면 파내기는 사용자가 그린 선이 있음
                addBossEdges(out, solids, i + 1, fp, tol, &ei.rings, reach);
        // 정면: 옆면 구멍(구멍R·구멍P 를 측면·평면도에 놓은 것)을 숨은선 두 줄로 (구멍 단면으로 그린 것은 사용자 선이 있음)
        for (int i = 0; i < holes.size(); ++i) {
            const PlateHole &h = holes.at(i);
            if (h.fromSection || std::fabs(h.dir.z()) > 0.5)
                continue;
            const QPointF e(h.entry.x(), h.entry.y());
            const QPointF d(h.dir.x(), h.dir.y());
            const QPointF w(-d.y(), d.x());
            const double r = h.hole.diameter * 0.5, L = holeLength(h);
            for (int sgn = -1; sgn <= 1; sgn += 2)
                out.hidden << QLineF(e + w * (sgn * r), e + w * (sgn * r) + d * L);
        }
        return out;
    }

    ViewAxes ax;
    if (!viewAxes(kind, a0, param, ax))
        return out;
    // 꼭짓점의 뷰 좌표 s 범위 = 외형 사각
    double smin = 0.0, smax = 0.0;
    sRange(ei.rings, a0, ax, smin, smax);
    out.visible << QLineF(viewPt(ax, smin, z0), viewPt(ax, smax, z0)) << QLineF(viewPt(ax, smax, z0), viewPt(ax, smax, z1))
                << QLineF(viewPt(ax, smax, z1), viewPt(ax, smin, z1)) << QLineF(viewPt(ax, smin, z1), viewPt(ax, smin, z0));

    // 꼭짓점마다 두께 방향 선 하나: 꺾인 곳(면과 면의 모서리) 또는 곡선의 s 극값(원통면 윤곽).
    //   그 s 에서 보는 쪽 가장 앞(w 최대)이면 실선, 뒤에 가려지면 숨은선. 오목한 모양·섬도 같은 규칙. (tol = 위 입체 판정과 같음)
    const double ds = std::max(1e-9, size * 1e-6);
    const double creaseCos = std::cos(qDegreesToRadians(20.0));
    QList<double> visS, hidS;
    for (int r = 0; r < ei.rings.size(); ++r) {
        const QPolygonF &p = ei.rings.at(r);
        const int nv = p.size();
        if (nv < 3)
            continue;
        for (int k = 0; k < nv; ++k) {
            const QPointF prev = p.at((k + nv - 1) % nv), cur = p.at(k), next = p.at((k + 1) % nv);
            const double sv = dot2(cur - a0, ax.u);
            if (sv <= smin + eps || sv >= smax - eps)
                continue;                               // 외형선과 겹침
            const QPointF d1 = cur - prev, d2 = next - cur;
            const double l1 = std::hypot(d1.x(), d1.y()), l2 = std::hypot(d2.x(), d2.y());
            if (l1 < eps || l2 < eps)
                continue;
            const bool crease = dot2(d1, d2) / (l1 * l2) < creaseCos;
            const double sp = dot2(prev - a0, ax.u), sn = dot2(next - a0, ax.u);
            const bool extreme = (sp - sv) * (sn - sv) > 0.0;
            if (!crease && !extreme)
                continue;
            double front = -1e300;
            for (int sg = -1; sg <= 1; sg += 2) {
                const QList<double> ws = crossingsW(ei.rings, a0, ax, sv + sg * ds);
                if (!ws.isEmpty())
                    front = std::max(front, ws.last());
            }
            const bool vis = dot2(cur - a0, ax.n) >= front - tol;
            addUnique(vis ? visS : hidS, sv);
        }
    }
    for (int i = 0; i < visS.size(); ++i)
        out.visible << QLineF(viewPt(ax, visS.at(i), z0), viewPt(ax, visS.at(i), z1));
    for (int i = 0; i < hidS.size(); ++i) {
        bool dup = false;
        for (int j = 0; j < visS.size() && !dup; ++j)
            dup = std::fabs(visS.at(j) - hidS.at(i)) < 1e-6;
        if (!dup)
            out.hidden << QLineF(viewPt(ax, hidS.at(i), z0), viewPt(ax, hidS.at(i), z1));
    }

    // 돌기: 판 실선 중 돌기 뒤에 가려지는 곳은 숨은선으로, 돌기 모서리 추가 (이 뷰에 그린 돌기는 사용자 선이 있어 모서리 생략)
    if (!bosses.isEmpty()) {
        ViewProj vp;
        vp.ax = ax;
        vp.a0 = a0;
        vp.toward = QVector3D(float(ax.n.x()), float(ax.n.y()), 0.0f);
        QList<Solid> bossOnly;                          // 판 선을 가리는 것 = 돌기 (파내기는 가리지 않음)
        for (int j = 1; j < solids.size(); ++j)
            if (!solids.at(j).cut)
                bossOnly << solids.at(j);
        const QList<QLineF> plateVis = out.visible;
        out.visible.clear();
        for (int i = 0; i < plateVis.size(); ++i) {
            const QLineF &ln = plateVis.at(i);
            const int K = 24;
            int status[K];
            for (int k = 0; k < K; ++k) {
                const QPointF q = ln.pointAt((k + 0.5) / K);
                // 뷰 점 → 판 표면의 3D 점 (그 s 에서 보는 쪽 가장 앞)
                const QPointF d = q - ax.c;
                const double sv = std::max(smin + ds2(size), std::min(smax - ds2(size), dot2(d, ax.u)));
                const double Z = -dot2(d, ax.n);
                const QList<double> ws = crossingsW(ei.rings, a0, ax, sv);
                int st = 1;
                if (!ws.isEmpty()) {
                    const QPointF xy = a0 + ax.u * sv + ax.n * ws.last();
                    const QVector3D P(float(xy.x()), float(xy.y()), float(Z));
                    for (int j = 0; j < bossOnly.size() && st == 1; ++j)
                        if (rayHits(bossOnly.at(j), P + vp.toward * float(tol), vp.toward, tol))
                            st = 2;
                }
                status[k] = st;
            }
            int k0 = 0;
            while (k0 < K) {
                int k1 = k0;
                while (k1 + 1 < K && status[k1 + 1] == status[k0])
                    ++k1;
                const QLineF piece(ln.pointAt(double(k0) / K), ln.pointAt(double(k1 + 1) / K));
                (status[k0] == 1 ? out.visible : out.hidden) << piece;
                k0 = k1 + 1;
            }
        }
        for (int i = 0; i < bosses.size(); ++i)
            if (bosses.at(i).plateFront || viewId.isEmpty() || bosses.at(i).viewId != viewId)
                addBossEdges(out, solids, i + 1, vp, tol, nullptr, reach);
    }

    // 구멍
    for (int i = 0; i < holes.size(); ++i) {
        const PlateHole &h = holes.at(i);
        if (!viewId.isEmpty() && h.viewId == viewId)
            continue;                                   // 이 뷰에 사용자가 놓은 구멍
        const double r = h.hole.diameter * 0.5, L = holeLength(h);
        const QVector3D e = h.entry, d = h.dir;
        const QVector3D f = e + d * float(L);
        const QPointF e2(e.x(), e.y()), f2(f.x(), f.y()), d2(d.x(), d.y());
        const double se = dot2(e2 - a0, ax.u), sf = dot2(f2 - a0, ax.u);
        const double dn = dot2(d2, ax.n);
        if (std::fabs(dn) > 0.9) {                      // 보는 방향과 나란함 → 원 (그 면에서 들어가면 실선)
            const bool open = h.hole.throughHole || dn < 0.0;
            (open ? out.visibleCircles : out.hiddenCircles) << ProjCircle{viewPt(ax, se, e.z()), r};
        } else if (std::fabs(d.z()) > 0.9) {            // 앞면 구멍 (Z 방향) → s ± r 숨은선
            for (int sg = -1; sg <= 1; sg += 2)
                out.hidden << QLineF(viewPt(ax, se + sg * r, e.z()), viewPt(ax, se + sg * r, f.z()));
        } else if (std::fabs(sf - se) > 1e-6) {          // 도면 안 방향(비스듬해도) → Z ± r 숨은선 (타원은 생략)
            for (int sg = -1; sg <= 1; sg += 2)
                out.hidden << QLineF(viewPt(ax, se, e.z() + sg * r), viewPt(ax, sf, e.z() + sg * r));
        }
    }
    if (!showHidden) {
        out.hidden.clear();
        out.hiddenCircles.clear();
    }
    return out;
}

bool PlateView::faceAt(MyGraphicsScene *scene, const QString &uuid, const QPointF &pos, double &normalDeg,
                       QLineF *edge, double maxDist)
{
    ExtrudeInfo ei;
    QPointF a0, ref;
    if (!frame(scene, uuid, ei, a0, ref))
        return false;
    double best = 1e300;
    QLineF bestEdge;
    for (int r = 0; r < ei.rings.size(); ++r) {
        const QPolygonF &p = ei.rings.at(r);
        for (int k = 0; k < p.size(); ++k) {
            const QPointF a = p.at(k), b = p.at((k + 1) % p.size());
            const QPointF d = b - a;
            const double len2 = dot2(d, d);
            if (len2 < 1e-12)
                continue;
            const double t = std::max(0.0, std::min(1.0, dot2(pos - a, d) / len2));
            const QPointF q = a + d * t;
            const double dist = std::hypot(pos.x() - q.x(), pos.y() - q.y());
            if (dist < best) {
                best = dist;
                bestEdge = QLineF(a, b);
            }
        }
    }
    if (best > 1e299 || (maxDist > 0.0 && best > maxDist))
        return false;
    // 고리 방향 정리됨 (바깥 = 반시계, 섬 = 시계) → 변 a→b 의 오른쪽이 늘 재료 바깥
    const QPointF d = bestEdge.p2() - bestEdge.p1();
    normalDeg = qRadiansToDegrees(std::atan2(-d.x(), d.y()));
    if (edge)
        *edge = bestEdge;
    return true;
}

bool PlateView::viewAt(MyGraphicsScene *scene, const QPointF &pos, QString &plateUuid, FaceView &view)
{
    if (!scene)
        return false;
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        if (it.value().type != QLatin1String("MechExtrude") || !it.value().props.value(QStringLiteral("Standalone")).toBool())
            continue;
        const QList<FaceView> fvs = views(scene, it.key());
        for (int i = 0; i < fvs.size(); ++i)
            if (fvs.at(i).poly.containsPoint(pos, Qt::OddEvenFill)) {
                plateUuid = it.key();
                view = fvs.at(i);
                return true;
            }
    }
    return false;
}

bool PlateView::bossSection(MyGraphicsScene *scene, const QString &plateUuid, const QString &viewId,
                            const QPainterPath &region, double depth, bool merge, ExtrudeInfo &info)
{
    ExtrudeInfo pe;
    QPointF a0, ref;
    if (!frame(scene, plateUuid, pe, a0, ref))
        return false;
    const QList<FaceView> fvs = views(scene, plateUuid);
    const FaceView *fv = nullptr;
    for (int i = 0; i < fvs.size() && !fv; ++i)
        if (fvs.at(i).viewId == viewId)
            fv = &fvs.at(i);
    if (!fv)
        return false;
    ViewAxes ax;
    ax.c = fv->center;
    ax.n = fv->n;
    ax.u = fv->u;
    // 도면 → (s, Z)
    QList<QPolygonF> rings;
    const QList<QPolygonF> polys = region.toSubpathPolygons();
    for (int i = 0; i < polys.size(); ++i) {
        QPolygonF r;
        for (int k = 0; k < polys.at(i).size(); ++k) {
            const QPointF d = polys.at(i).at(k) - ax.c;
            const QPointF q(dot2(d, ax.u), -dot2(d, ax.n));
            if (!r.isEmpty() && QLineF(r.last(), q).length() < 1e-6)
                continue;
            r << q;
        }
        while (r.size() > 1 && QLineF(r.first(), r.last()).length() < 1e-6)
            r.removeLast();
        if (r.size() >= 3 && std::fabs(ringSigned(r)) > 1e-6)
            rings << r;
    }
    if (rings.isEmpty())
        return false;
    // 고리 방향: 바깥 반시계, 안쪽 구멍 시계 (다른 고리 안에 든 횟수)
    QList<QPolygonF> oriented;
    for (int i = 0; i < rings.size(); ++i) {
        int depthN = 0;
        const QPointF probe = (rings.at(i).at(0) + rings.at(i).at(1)) * 0.5;
        for (int j = 0; j < rings.size(); ++j)
            if (j != i && std::fabs(ringSigned(rings.at(j))) > std::fabs(ringSigned(rings.at(i))) &&
                rings.at(j).containsPoint(probe, Qt::OddEvenFill))
                ++depthN;
        QPolygonF r = rings.at(i);
        const bool hole = (depthN % 2) == 1;
        if ((!hole && ringSigned(r) < 0.0) || (hole && ringSigned(r) > 0.0))
            std::reverse(r.begin(), r.end());
        oriented << r;
    }
    // 단면 s 범위 안의 판 표면(보는 쪽 가장 앞) — 가장 깊은 곳·가장 앞
    double smin = 1e300, smax = -1e300;
    for (int i = 0; i < oriented.size(); ++i)
        for (int k = 0; k < oriented.at(i).size(); ++k) {
            smin = std::min(smin, oriented.at(i).at(k).x());
            smax = std::max(smax, oriented.at(i).at(k).x());
        }
    QList<double> samples;
    const int N = 64;
    for (int k = 0; k <= N; ++k)
        samples << smin + (smax - smin) * (double(k) / N);
    for (int r = 0; r < pe.rings.size(); ++r)
        for (int k = 0; k < pe.rings.at(r).size(); ++k) {
            const double sv = dot2(pe.rings.at(r).at(k) - a0, ax.u);
            if (sv > smin && sv < smax) {
                samples << sv - 1e-6 << sv + 1e-6;
            }
        }
    double wMin = 1e300, wMax = -1e300, backMin = 1e300;
    for (int i = 0; i < samples.size(); ++i) {
        const QList<double> ws = crossingsW(pe.rings, a0, ax, samples.at(i));
        if (ws.isEmpty())
            continue;
        wMin = std::min(wMin, ws.last());
        wMax = std::max(wMax, ws.last());
        backMin = std::min(backMin, ws.first());
    }
    if (wMin > 1e299)
        return false;                                   // 판 위가 아님
    info.rings = oriented;
    info.a0 = a0;
    info.angleDeg = qRadiansToDegrees(std::atan2(ax.n.y(), ax.n.x()));
    if (info.cut) {
        // 파내기: 표면 가장 앞 ~ (가장 깊은 표면 − 깊이, 관통이면 반대쪽 면) — 3D 커터는 밖으로 더 내밈
        info.x1 = wMax;
        info.x0 = info.through ? backMin : wMin - depth;
    } else {
        info.x0 = wMin - (merge ? kBossOverlap : 0.0);  // 합치기: 판 속으로 살짝 (Union 안정)
        info.x1 = wMax + depth;
    }
    info.plateBoss = true;
    return info.x1 - info.x0 > 1e-6;
}

QList<ExtrudeInfo> PlateView::bosses(MyGraphicsScene *scene, const QString &plateUuid)
{
    QList<ExtrudeInfo> out;
    if (!scene)
        return out;
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        const QMap<QString, QVariant> &pp = it.value().props;
        if (it.value().type != QLatin1String("MechExtrude") ||
            !(pp.value(QStringLiteral("PlateBoss")).toBool() || pp.value(QStringLiteral("PlateFront")).toBool()) ||
            pp.value(QStringLiteral("Host")).toString() != plateUuid)
            continue;
        ExtrudeInfo bi;
        if (ExtrudeFeature::resolve(scene, it.key(), bi))
            out << bi;
    }
    return out;
}

QString PlateView::plateAt(MyGraphicsScene *scene, const QPointF &pos, bool outerOnly, const QStringList &skip)
{
    if (!scene)
        return QString();
    // 판이 여러 겹 쌓여 있으면(판 위에 올린 돌출) 그 점에서 가장 앞(Z 가 큰) 판을 고른다
    //   — 돌출 시작 Z 자동·정면 파내기가 맨 위 면에서 시작하도록
    QString best;
    double bestTop = -1e300;
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        if (it.value().type != QLatin1String("MechExtrude") || !it.value().props.value(QStringLiteral("Standalone")).toBool())
            continue;
        if (skip.contains(it.key()))
            continue;                                   // 부르는 쪽이 뺀 판
        ExtrudeInfo ei;
        QPointF a0, ref;
        if (!frame(scene, it.key(), ei, a0, ref))
            continue;
        QList<QPolygonF> rs;
        for (int r = 0; r < ei.rings.size(); ++r)
            if (!outerOnly || ringSigned(ei.rings.at(r)) > 0.0)
                rs << ei.rings.at(r);
        if (ei.boolRole == ExtrudeInfo::BoolConsumed || ei.boolRole == ExtrudeInfo::BoolCutter)
            continue;                                   // 부품 합치기의 도구 판 (A 에 들어감)
        if (ringsPath(rs).contains(pos) && ei.x1 > bestTop) {
            bestTop = ei.x1;
            best = it.key();
        }
    }
    return best;
}
