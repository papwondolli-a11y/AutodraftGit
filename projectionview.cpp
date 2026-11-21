#include "projectionview.h"

#include "abstracttool.h"
#include "cadcommands.h"
#include "caditem.h"
#include "drawingsettingscommand.h"
#include "entityfactory.h"
#include "extrudefeature.h"
#include "holedata.h"
#include "hatchitem.h"
#include "holeitem.h"
#include "holesectiongenerator.h"
#include "holesync.h"
#include "iappcontext.h"
#include "layer.h"
#include "layermanager.h"
#include "mechmodel3d.h"
#include "mechpartdata.h"
#include "mygraphicsscene.h"
#include "scenesymbolregistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUndoStack>
#include <QUuid>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {

const double kEpsX = 1e-6;
const double kDelta = 1e-4;       // 시선 높이 위·아래로 살짝 (재료 안 판정)
const double kCenterOver = 3.0;   // 중심선을 외형 밖으로 내미는 길이 (mm)

struct Interval {
    double a;
    double b;
};

// 수평선 y 와 단면 고리들이 만나는 x 들 → 재료 구간 (짝홀 규칙)
QList<Interval> materialAtY(const QList<RevolveLoop> &loops, double y)
{
    QList<double> xs;
    for (int i = 0; i < loops.size(); ++i) {
        const QPolygonF &p = loops.at(i).pts;
        for (int k = 0; k < p.size(); ++k) {
            const QPointF a = p.at(k), b = p.at((k + 1) % p.size());
            if ((a.y() > y) == (b.y() > y))
                continue;
            xs << a.x() + (y - a.y()) * (b.x() - a.x()) / (b.y() - a.y());
        }
    }
    std::sort(xs.begin(), xs.end());
    QList<Interval> out;
    for (int i = 0; i + 1 < xs.size(); i += 2)
        out << Interval{xs.at(i), xs.at(i + 1)};
    return out;
}

// 높이 r 의 시선(축과 나란히)이 재료 "속"을 지나는 구간 = r±δ 양쪽 모두 재료인 곳
QList<Interval> solidAtR(const QList<RevolveLoop> &loops, double r)
{
    const QList<Interval> up = materialAtY(loops, r + kDelta);
    const QList<Interval> dn = materialAtY(loops, r - kDelta);
    QList<Interval> out;
    for (int i = 0; i < up.size(); ++i)
        for (int k = 0; k < dn.size(); ++k) {
            const double a = std::max(up.at(i).a, dn.at(k).a);
            const double b = std::min(up.at(i).b, dn.at(k).b);
            if (b - a > 1e-6)
                out << Interval{a, b};
        }
    return out;
}

// 각 x 에서 가장 바깥 반지름 R(x)
double outerR(const QList<RevolveLoop> &loops, double x)
{
    double best = 0.0;
    for (int i = 0; i < loops.size(); ++i) {
        const QPolygonF &p = loops.at(i).pts;
        for (int k = 0; k < p.size(); ++k) {
            const QPointF a = p.at(k), b = p.at((k + 1) % p.size());
            if ((a.x() > x) == (b.x() > x))
                continue;
            best = std::max(best, a.y() + (x - a.x()) * (b.y() - a.y()) / (b.x() - a.x()));
        }
    }
    return best;
}

// 원(모서리)이 생기는 꼭짓점: 꺾임 15° 넘음 또는 반지름 극값(곡면의 윤곽)
bool isEdgeVertex(const QPolygonF &p, int k)
{
    const QPointF prev = p.at((k + p.size() - 1) % p.size()), cur = p.at(k), next = p.at((k + 1) % p.size());
    if (cur.y() < 1e-6)
        return false;
    const QPointF d1 = cur - prev, d2 = next - cur;
    const double l1 = std::hypot(d1.x(), d1.y()), l2 = std::hypot(d2.x(), d2.y());
    if (l1 < 1e-9 || l2 < 1e-9)
        return false;
    const double cosA = (d1.x() * d2.x() + d1.y() * d2.y()) / (l1 * l2);
    if (cosA < std::cos(qDegreesToRadians(15.0)))
        return true;
    return (prev.y() < cur.y() && next.y() < cur.y()) || (prev.y() > cur.y() && next.y() > cur.y());
}

void addUniqueCircle(QList<double> &list, double r)
{
    for (int i = 0; i < list.size(); ++i)
        if (std::fabs(list.at(i) - r) < 1e-4)
            return;
    list << r;
}

// 같은 x 의 세로선: 실선이 덮는 숨은선은 버린다
struct VLine {
    double x;
    double r;   // ±r
    bool visible;
};

// 축 좌표 선분 (y ≥ 0 쪽) — 나중에 ±y 로 그린다
struct ALine {
    QPointF a;
    QPointF b;
    bool visible;
};

bool coveredBy(const ALine &h, const QList<ALine> &vis)
{
    for (int i = 0; i < vis.size(); ++i) {
        const ALine &v = vis.at(i);
        if (!v.visible)
            continue;
        const QPointF dv = v.b - v.a, dh = h.b - h.a;
        const double cross = dv.x() * dh.y() - dv.y() * dh.x();
        if (std::fabs(cross) > 1e-6 * (std::hypot(dv.x(), dv.y()) * std::hypot(dh.x(), dh.y()) + 1e-12))
            continue;
        const QPointF w = h.a - v.a;
        if (std::fabs(dv.x() * w.y() - dv.y() * w.x()) > 1e-6 * std::hypot(dv.x(), dv.y()))
            continue;
        const double L2 = dv.x() * dv.x() + dv.y() * dv.y();
        const double t0 = ((h.a - v.a).x() * dv.x() + (h.a - v.a).y() * dv.y()) / L2;
        const double t1 = ((h.b - v.a).x() * dv.x() + (h.b - v.a).y() * dv.y()) / L2;
        if (std::min(t0, t1) >= -1e-6 && std::max(t0, t1) <= 1.0 + 1e-6)
            return true;
    }
    return false;
}

// 길이 0 조각은 버리고, 한 직선 위에서 맞닿거나 겹치는 조각은 하나로 합친다
double offLine(const QLineF &A, const QPointF &q)
{
    const QPointF d = A.p2() - A.p1();
    const double L = std::hypot(d.x(), d.y());
    const QPointF w = q - A.p1();
    return std::fabs(w.x() * d.y() - w.y() * d.x()) / L;
}

double alongLine(const QLineF &A, const QPointF &q)
{
    const QPointF d = A.p2() - A.p1();
    const double L = std::hypot(d.x(), d.y());
    return ((q - A.p1()).x() * d.x() + (q - A.p1()).y() * d.y()) / L;
}

void mergeCollinear(QList<QLineF> &lines)
{
    QList<QLineF> merged;
    for (int a = 0; a < lines.size(); ++a)
        if (lines.at(a).length() > 1e-6)
            merged << lines.at(a);
    bool changed = true;
    while (changed) {
        changed = false;
        for (int a = 0; a < merged.size() && !changed; ++a)
            for (int b = a + 1; b < merged.size() && !changed; ++b) {
                const QLineF A = merged.at(a), B = merged.at(b);
                if (offLine(A, B.p1()) > 1e-6 || offLine(A, B.p2()) > 1e-6)
                    continue;
                const double LA = A.length();
                const double b0 = std::min(alongLine(A, B.p1()), alongLine(A, B.p2()));
                const double b1 = std::max(alongLine(A, B.p1()), alongLine(A, B.p2()));
                if (b0 > LA + 1e-6 || b1 < -1e-6)
                    continue;                                   // 떨어져 있음
                const QPointF u = (A.p2() - A.p1()) / LA;
                merged[a] = QLineF(A.p1() + u * std::min(0.0, b0), A.p1() + u * std::max(LA, b1));
                merged.removeAt(b);
                changed = true;
            }
    }
    lines = merged;
}

QString hiddenLayer(IAppContext *ctx)
{
    const QString name = QStringLiteral("숨은선");
    if (!ctx->layerManager()->getLayer(name)) {
        Layer layer(name, QColor(255, 255, 0), 0.18, QStringLiteral("DashLine(small)"), false, true, true, false);
        ctx->layerManager()->addLayer(layer);
    }
    return name;
}

QString centerLayer(IAppContext *ctx)
{
    QString name = DrawingSettingsCommand::s_centerLineLayer;
    if (!name.isEmpty() && ctx->layerManager()->getLayer(name))
        return name;
    name = QStringLiteral("중심선");
    if (!ctx->layerManager()->getLayer(name)) {
        Layer layer(name, QColor(255, 0, 0), 0.18, QStringLiteral("CENTER"), false, true, true, false);
        ctx->layerManager()->addLayer(layer);
    }
    return name;
}

void addItem(IAppContext *ctx, MyGraphicsScene *scene, QGraphicsItem *item, const QString &layer,
             const QString &viewId)
{
    if (!item)
        return;
    if (CadItem *cad = dynamic_cast<CadItem *>(item))
        cad->setLayerName(layer);
    item->setData(MECH_VIEW_ID, viewId);
    item->setParentItem(scene->getLayerRoot(layer));
    if (item->scene() == scene)
        scene->removeItem(item);
    if (ctx->undoStack())
        ctx->undoStack()->push(new AddEntityCommand(scene, item));
    else
        scene->addItem(item);
}

QJsonArray viewsOf(const SymbolInfo &info)
{
    return QJsonDocument::fromJson(info.props.value(QStringLiteral("Views")).toString().toUtf8()).array();
}

} // namespace

bool ProjDrawing::isEmpty() const
{
    return visible.isEmpty() && hidden.isEmpty() && visibleCircles.isEmpty() && hiddenCircles.isEmpty();
}

QPainterPath ProjDrawing::path() const
{
    QPainterPath p;
    const QList<QLineF> *lists[3] = {&visible, &hidden, &center};
    for (int l = 0; l < 3; ++l)
        for (int i = 0; i < lists[l]->size(); ++i) {
            p.moveTo(lists[l]->at(i).p1());
            p.lineTo(lists[l]->at(i).p2());
        }
    const QList<ProjCircle> *cl[2] = {&visibleCircles, &hiddenCircles};
    for (int l = 0; l < 2; ++l)
        for (int i = 0; i < cl[l]->size(); ++i)
            p.addEllipse(cl[l]->at(i).c, cl[l]->at(i).r, cl[l]->at(i).r);
    return p;
}

QString ProjectionView::kindName(int kind)
{
    if (kind == Left)
        return QStringLiteral("좌측면도");
    if (kind == Right)
        return QStringLiteral("우측면도");
    if (kind == Front)
        return QStringLiteral("정면 구멍선");
    return QStringLiteral("평면도(외형)");
}

double ProjectionView::paramFor(int kind, const QPointF &a0, double angleDeg, const QPointF &p)
{
    const double ang = qDegreesToRadians(angleDeg);
    const QPointF u(std::cos(ang), std::sin(ang)), n(-std::sin(ang), std::cos(ang));
    const QPointF d = p - a0;
    return (kind == Top || kind == Front) ? (d.x() * n.x() + d.y() * n.y()) : (d.x() * u.x() + d.y() * u.y());
}

QPointF ProjectionView::anchorFor(int kind, const QPointF &a0, double angleDeg, double param)
{
    const double ang = qDegreesToRadians(angleDeg);
    const QPointF u(std::cos(ang), std::sin(ang)), n(-std::sin(ang), std::cos(ang));
    return a0 + ((kind == Top || kind == Front) ? n : u) * param;
}

ProjDrawing ProjectionView::build(const QList<RevolveLoop> &loops, int kind, const QPointF &a0, double angleDeg,
                                  double param, bool showHidden, const QList<HoleFeature> &holes)
{
    ProjDrawing d = buildAll(loops, kind, a0, angleDeg, param, holes);
    if (!showHidden) {
        d.hidden.clear();
        d.hiddenCircles.clear();
    }
    return d;
}

ProjDrawing ProjectionView::buildAll(const QList<RevolveLoop> &loops, int kind, const QPointF &a0, double angleDeg,
                                     double param, const QList<HoleFeature> &holes)
{
    ProjDrawing out;
    if (loops.isEmpty())
        return out;
    const double ang = qDegreesToRadians(angleDeg);
    const QPointF u(std::cos(ang), std::sin(ang)), n(-std::sin(ang), std::cos(ang));

    double xMin = 1e300, xMax = -1e300, rMax = 0.0;
    for (int i = 0; i < loops.size(); ++i)
        for (int k = 0; k < loops.at(i).pts.size(); ++k) {
            const QPointF q = loops.at(i).pts.at(k);
            xMin = std::min(xMin, q.x());
            xMax = std::max(xMax, q.x());
            rMax = std::max(rMax, q.y());
        }

    if (kind == Left || kind == Right) {
        // 원마다: 같은 반지름 꼭짓점 중 하나라도 보이면 실선
        QList<double> vis, hid;
        for (int i = 0; i < loops.size(); ++i) {
            const QPolygonF &p = loops.at(i).pts;
            for (int k = 0; k < p.size(); ++k) {
                if (!isEdgeVertex(p, k))
                    continue;
                const double xv = p.at(k).x(), rv = p.at(k).y();
                bool blocked = false;
                const QList<Interval> solid = solidAtR(loops, rv);
                for (int s = 0; s < solid.size() && !blocked; ++s)
                    blocked = (kind == Left) ? (solid.at(s).a < xv - 1e-5) : (solid.at(s).b > xv + 1e-5);
                if (blocked)
                    addUniqueCircle(hid, rv);
                else
                    addUniqueCircle(vis, rv);
            }
        }
        const QPointF c = a0 + u * param;
        for (int i = 0; i < vis.size(); ++i)
            out.visibleCircles << ProjCircle{c, vis.at(i)};
        for (int i = 0; i < hid.size(); ++i) {
            bool dup = false;
            for (int k = 0; k < vis.size() && !dup; ++k)
                dup = std::fabs(vis.at(k) - hid.at(i)) < 1e-4;
            if (!dup)
                out.hiddenCircles << ProjCircle{c, hid.at(i)};
        }
        const double L = rMax + kCenterOver;
        out.center << QLineF(c - u * L, c + u * L) << QLineF(c - n * L, c + n * L);
        // 축 방향 구멍 (다른 측면도에 그린 HoleItem): 좌측면도 오른쪽 = +z, 우측면도 오른쪽 = −z
        const bool leftSide = (kind == Left);
        for (int h = 0; h < holes.size(); ++h) {
            const HoleFeature &hf = holes.at(h);
            if (hf.leftSide == leftSide)
                continue;                                   // 이 쪽 측면도에 사용자가 그린 구멍
            double hx0 = 0.0, hx1 = 0.0;
            if (!hf.xRange(loops, hx0, hx1))
                continue;
            const QPointF hc = c + u * (leftSide ? hf.z : -hf.z) + n * hf.y;
            (hf.openToward(leftSide) ? out.visibleCircles : out.hiddenCircles) << ProjCircle{hc, hf.hole.diameter * 0.5};
        }
        return out;
    }

    if (kind == Front)
        return out;                                         // 정면 구멍 단면은 drawView 에서 (구멍 단면 생성기)

    // ── 평면(외형): 축과 수직 방향에서 본 모습 ──
    QList<double> breaks;
    for (int i = 0; i < loops.size(); ++i)
        for (int k = 0; k < loops.at(i).pts.size(); ++k)
            breaks << loops.at(i).pts.at(k).x();
    std::sort(breaks.begin(), breaks.end());

    QList<ALine> lines;
    QList<VLine> vlines;
    for (int i = 0; i < loops.size(); ++i) {
        const QPolygonF &p = loops.at(i).pts;
        for (int k = 0; k < p.size(); ++k) {
            const QPointF a = p.at(k), b = p.at((k + 1) % p.size());
            if (a.y() < 1e-6 && b.y() < 1e-6)
                continue;                                        // 축 위의 변
            if (std::fabs(a.x() - b.x()) < kEpsX) {               // 끝면·턱면 (옆에서 보면 세로선)
                const double rt = std::max(a.y(), b.y());
                const double R = std::max(outerR(loops, a.x() - 1e-4), outerR(loops, a.x() + 1e-4));
                vlines << VLine{a.x(), rt, rt >= R - 1e-6};
                continue;
            }
            // 기울거나 수평인 변: 꼭짓점 x 마다 잘라 조각별 판정
            const double x0 = std::min(a.x(), b.x()), x1 = std::max(a.x(), b.x());
            QList<double> cuts;
            cuts << x0;
            for (int t = 0; t < breaks.size(); ++t)
                if (breaks.at(t) > x0 + kEpsX && breaks.at(t) < x1 - kEpsX)
                    cuts << breaks.at(t);
            cuts << x1;
            for (int t = 0; t + 1 < cuts.size(); ++t) {
                const double sa = cuts.at(t), sb = cuts.at(t + 1);
                if (sb - sa < kEpsX)
                    continue;
                const double ra = a.y() + (sa - a.x()) * (b.y() - a.y()) / (b.x() - a.x());
                const double rb = a.y() + (sb - a.x()) * (b.y() - a.y()) / (b.x() - a.x());
                const double xm = (sa + sb) * 0.5, rm = (ra + rb) * 0.5;
                lines << ALine{QPointF(sa, ra), QPointF(sb, rb), rm >= outerR(loops, xm) - 1e-6};
            }
        }
        // 각진 꼭짓점의 원 모서리 → 세로선
        for (int k = 0; k < p.size(); ++k) {
            if (!isEdgeVertex(p, k))
                continue;
            const double xv = p.at(k).x(), rv = p.at(k).y();
            const double R = std::max(outerR(loops, xv - 1e-4), outerR(loops, xv + 1e-4));
            vlines << VLine{xv, rv, rv >= R - 1e-6};
        }
    }

    const QPointF base = a0 + n * param;
    // 세로선: 같은 x 에서 가장 긴 실선 하나 + 그보다 긴 숨은선만
    QList<VLine> vfinal;
    for (int i = 0; i < vlines.size(); ++i) {
        const VLine &v = vlines.at(i);
        bool skip = false;
        for (int k = 0; k < vlines.size() && !skip; ++k) {
            if (k == i || std::fabs(vlines.at(k).x - v.x) > 1e-5)
                continue;
            const VLine &o = vlines.at(k);
            if (o.visible && o.r >= v.r - 1e-6 && (!v.visible || o.r > v.r + 1e-6 || k < i))
                skip = true;                                    // 실선이 덮음 (같으면 앞의 것만)
            else if (!o.visible && !v.visible && std::fabs(o.r - v.r) < 1e-6 && k < i)
                skip = true;
        }
        if (!skip)
            vfinal << v;
    }
    for (int i = 0; i < vfinal.size(); ++i) {
        const VLine &v = vfinal.at(i);
        const QLineF ln(base + u * v.x - n * v.r, base + u * v.x + n * v.r);
        (v.visible ? out.visible : out.hidden) << ln;
    }
    for (int i = 0; i < lines.size(); ++i) {
        const ALine &l = lines.at(i);
        if (!l.visible && coveredBy(l, lines))
            continue;
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            const QLineF ln(base + u * l.a.x() + n * (sgn * l.a.y()), base + u * l.b.x() + n * (sgn * l.b.y()));
            (l.visible ? out.visible : out.hidden) << ln;
        }
    }
    out.center << QLineF(base + u * (xMin - kCenterOver), base + u * (xMax + kCenterOver));
    for (int h = 0; h < holes.size(); ++h) {
        const HoleFeature &hf = holes.at(h);
        double hx0 = 0.0, hx1 = 0.0;
        if (!hf.xRange(loops, hx0, hx1))
            continue;
        const double off = -hf.z;                           // 평면도 위쪽 = −z
        for (int sgn = -1; sgn <= 1; sgn += 2)
            out.hidden << QLineF(base + u * hx0 + n * (off + sgn * hf.hole.diameter * 0.5),
                                 base + u * hx1 + n * (off + sgn * hf.hole.diameter * 0.5));
    }
    return out;
}

bool ProjectionView::drawView(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid,
                              const QString &viewId, int kind, double param, bool showHidden, QString *error)
{
    if (!ctx || !scene || !ctx->entityFactory() || !ctx->layerManager())
        return false;
    QList<RevolveLoop> loops;
    QPointF a0;
    double angleDeg = 0.0;
    QString name;
    if (!MechModel3D::revolveSection(scene, partUuid, loops, a0, angleDeg, name)) {
        if (error)
            *error = QStringLiteral("이 부품은 투상도를 만들 수 있는 회전체가 아닙니다.");
        return false;
    }
    ProjDrawing d = build(loops, kind, a0, angleDeg, param, showHidden, holesOf(scene, partUuid));
    if (kind == Front) {
        drawFrontMirror(ctx, scene, partUuid, viewId, a0, angleDeg);
        drawFrontHoles(ctx, scene, partUuid, viewId, loops, a0, angleDeg);
        drawExtrudes(ctx, scene, partUuid, viewId, kind, param, showHidden, loops, a0, angleDeg);
        return true;
    }
    if (d.isEmpty() && d.center.isEmpty()) {
        if (error)
            *error = QStringLiteral("투상도를 만들 선이 없습니다.");
        return false;
    }
    Layer *cur = ctx->layerManager()->getCurrentLayer();
    const QString visLayer = cur ? cur->name() : QStringLiteral("0");
    const QString hidLayer = showHidden ? hiddenLayer(ctx) : visLayer;
    const QString cenLayer = centerLayer(ctx);
    const QPen visPen = AbstractTool::layerPenFor(ctx, visLayer);
    const QPen hidPen = AbstractTool::layerPenFor(ctx, hidLayer);
    const QPen cenPen = AbstractTool::layerPenFor(ctx, cenLayer);
    EntityFactory *ef = ctx->entityFactory();
    for (int i = 0; i < d.visibleCircles.size(); ++i)
        addItem(ctx, scene, ef->createCircle(d.visibleCircles.at(i).c, d.visibleCircles.at(i).r, visPen), visLayer, viewId);
    for (int i = 0; i < d.hiddenCircles.size(); ++i)
        addItem(ctx, scene, ef->createCircle(d.hiddenCircles.at(i).c, d.hiddenCircles.at(i).r, hidPen), hidLayer, viewId);
    // 같은 선 두 번 그리지 않기 (예: 평면도에서 겹쳐 보이는 두 구멍)
    {
        QList<QLineF> *lists[3] = {&d.visible, &d.hidden, &d.center};
        for (int l = 0; l < 3; ++l) {
            QList<QLineF> uniq;
            for (int a = 0; a < lists[l]->size(); ++a) {
                const QLineF &ln = lists[l]->at(a);
                bool dup = false;
                for (int b = 0; b < uniq.size() && !dup; ++b) {
                    const QLineF &o = uniq.at(b);
                    dup = (QLineF(o.p1(), ln.p1()).length() < 1e-6 && QLineF(o.p2(), ln.p2()).length() < 1e-6) ||
                          (QLineF(o.p1(), ln.p2()).length() < 1e-6 && QLineF(o.p2(), ln.p1()).length() < 1e-6);
                }
                if (!dup)
                    uniq << ln;
            }
            mergeCollinear(uniq);
            *lists[l] = uniq;
        }
    }
    for (int i = 0; i < d.visible.size(); ++i)
        addItem(ctx, scene, ef->createLine(d.visible.at(i), visPen), visLayer, viewId);
    for (int i = 0; i < d.hidden.size(); ++i)
        addItem(ctx, scene, ef->createLine(d.hidden.at(i), hidPen), hidLayer, viewId);
    for (int i = 0; i < d.center.size(); ++i)
        addItem(ctx, scene, ef->createLine(d.center.at(i), cenPen), cenLayer, viewId);
    drawExtrudes(ctx, scene, partUuid, viewId, kind, param, showHidden, loops, a0, angleDeg);
    return true;
}

void ProjectionView::addViewRecord(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid,
                                   const QString &viewId, int kind, double param, bool showHidden)
{
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    if (!reg->hasSymbol(partUuid))
        return;
    const SymbolInfo oldInfo = reg->symbolInfo(partUuid);
    SymbolInfo info = oldInfo;
    QJsonArray views = viewsOf(oldInfo);
    QList<RevolveLoop> loops;
    QPointF a0, ref;
    double angleDeg = 0.0;
    QString name;
    QJsonObject v;
    if (MechModel3D::revolveSection(scene, partUuid, loops, a0, angleDeg, name, &ref)) {
        const QPointF anc = anchorFor(kind, a0, angleDeg, param);
        v.insert(QStringLiteral("ax"), anc.x());
        v.insert(QStringLiteral("ay"), anc.y());
        v.insert(QStringLiteral("rx"), ref.x());
        v.insert(QStringLiteral("ry"), ref.y());
    }
    v.insert(QStringLiteral("id"), viewId);
    v.insert(QStringLiteral("kind"), kind);
    v.insert(QStringLiteral("param"), param);
    v.insert(QStringLiteral("hidden"), showHidden);
    views.append(v);
    info.props[QStringLiteral("Views")] = QString::fromUtf8(QJsonDocument(views).toJson(QJsonDocument::Compact));
    if (ctx && ctx->undoStack())
        ctx->undoStack()->push(new UpdateRegistryCommand(reg, partUuid, oldInfo, info));
    else
        reg->registerSymbol(partUuid, info);
}

int ProjectionView::regeneratePart(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid)
{
    if (!ctx || !scene || !scene->symbolRegistry()->hasSymbol(partUuid))
        return 0;
    const QList<HoleFeature> holes = holesOf(scene, partUuid);
    if (!holes.isEmpty())
        ensureFrontView(ctx, scene, partUuid);
    // 이 부품 구멍들의 단면 선(구멍 단면 연동으로 다시 만들어져 뷰 태그가 없는 것 포함)은 정면 뷰가 새로 그린다
    {
        QSet<QString> gids;
        for (int h = 0; h < holes.size(); ++h)
            if (!holes.at(h).groupId.isEmpty())
                gids.insert(holes.at(h).groupId);
        const QList<QGraphicsItem *> items = scene->items();
        for (int k = 0; k < items.size(); ++k) {
            const QString g = items.at(k)->data(HOLE_SECTION_GROUP_ID).toString();
            if (!g.isEmpty() && gids.contains(g) && items.at(k)->data(MECH_VIEW_ID).toString().isEmpty()) {
                if (ctx->undoStack())
                    ctx->undoStack()->push(new RemoveEntityCommand(scene, items.at(k)));
                else
                    scene->removeItem(items.at(k));
            }
        }
    }
    const QJsonArray views = viewsOf(scene->symbolRegistry()->symbolInfo(partUuid));
    int count = 0;
    for (int i = 0; i < views.size(); ++i) {
        const QJsonObject v = views.at(i).toObject();
        const QString viewId = v.value(QStringLiteral("id")).toString();
        // 옛 선·원 지우기
        const QList<QGraphicsItem *> items = scene->items();
        for (int k = 0; k < items.size(); ++k)
            if (items.at(k)->data(MECH_VIEW_ID).toString() == viewId) {
                if (ctx->undoStack())
                    ctx->undoStack()->push(new RemoveEntityCommand(scene, items.at(k)));
                else
                    scene->removeItem(items.at(k));
            }
        // 뷰 기준점: 기록된 절대 위치 + 부품이 옮겨진 만큼 → 현재 축 기준 param 으로 다시 계산
        const int kind = v.value(QStringLiteral("kind")).toInt();
        double param = v.value(QStringLiteral("param")).toDouble();
        QList<RevolveLoop> loops;
        QPointF a0, ref;
        double angleDeg = 0.0;
        QString name;
        if (v.contains(QStringLiteral("ax")) &&
            MechModel3D::revolveSection(scene, partUuid, loops, a0, angleDeg, name, &ref)) {
            const QPointF anc(v.value(QStringLiteral("ax")).toDouble() + ref.x() - v.value(QStringLiteral("rx")).toDouble(),
                              v.value(QStringLiteral("ay")).toDouble() + ref.y() - v.value(QStringLiteral("ry")).toDouble());
            param = paramFor(kind, a0, angleDeg, anc);
        }
        if (drawView(ctx, scene, partUuid, viewId, kind, param, v.value(QStringLiteral("hidden")).toBool(), nullptr))
            ++count;
    }
    return count;
}

int ProjectionView::regenerateAll(IAppContext *ctx, MyGraphicsScene *scene)
{
    if (!ctx || !scene)
        return 0;
    const QMap<QString, SymbolInfo> all = scene->symbolRegistry()->allSymbols();
    int count = 0;
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it)
        count += regeneratePart(ctx, scene, it.key());
    return count;
}


void ProjectionView::ensureFrontView(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid)
{
    if (!scene || !scene->symbolRegistry()->hasSymbol(partUuid))
        return;
    const QJsonArray views = viewsOf(scene->symbolRegistry()->symbolInfo(partUuid));
    for (int i = 0; i < views.size(); ++i)
        if (views.at(i).toObject().value(QStringLiteral("kind")).toInt() == Front)
            return;
    addViewRecord(ctx, scene, partUuid, QUuid::createUuid().toString(QUuid::WithoutBraces), Front, 0.0, false);
}

QString ProjectionView::partAt(MyGraphicsScene *scene, const QPointF &pos)
{
    if (!scene)
        return QString();
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    const QList<QGraphicsItem *> items = scene->items(QRectF(pos.x() - 1.0, pos.y() - 1.0, 2.0, 2.0));
    for (int i = 0; i < items.size(); ++i) {
        const QString id = items.at(i)->data(MECH_DATA_PART_ID).toString();
        if (!id.isEmpty() && reg->hasSymbol(id))
            return id;
    }
    // 영역 방식 회전체: 고른 공간 안을 클릭
    const QMap<QString, SymbolInfo> &all = reg->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        if (it.value().type != QLatin1String("MechRevolve") ||
            it.value().props.value(QStringLiteral("Source")).toString() != QLatin1String("area"))
            continue;
        QList<QPainterPath> paths;
        QPointF ref;
        if (!RevolveProfile::partPaths(scene, it.key(), paths, ref))
            continue;
        for (int k = 0; k < paths.size(); ++k)
            if (paths.at(k).contains(pos))
                return it.key();
    }
    return QString();
}

// ─────────────────────────────────────────────────────────────────────────────
// 측면도·구멍 찾기 (그리기 탭 구멍R·구멍P 의 HoleItem)
// ─────────────────────────────────────────────────────────────────────────────
QList<ProjectionView::SideViewInfo> ProjectionView::sideViews(MyGraphicsScene *scene, const QString &partUuid)
{
    QList<SideViewInfo> out;
    if (!scene || !scene->symbolRegistry()->hasSymbol(partUuid))
        return out;
    const SymbolInfo info = scene->symbolRegistry()->symbolInfo(partUuid);
    if (info.type != QLatin1String("MechRevolve"))
        return out;
    const QJsonArray views = viewsOf(info);
    if (views.isEmpty())
        return out;
    QList<RevolveLoop> loops;
    QPointF a0, ref;
    double angleDeg = 0.0;
    QString name;
    if (!MechModel3D::revolveSection(scene, partUuid, loops, a0, angleDeg, name, &ref))
        return out;
    double rMax = 0.0;
    for (int i = 0; i < loops.size(); ++i)
        for (int k = 0; k < loops.at(i).pts.size(); ++k)
            rMax = std::max(rMax, loops.at(i).pts.at(k).y());
    const double ang = qDegreesToRadians(angleDeg);
    const QPointF u(std::cos(ang), std::sin(ang)), n(-std::sin(ang), std::cos(ang));
    for (int i = 0; i < views.size(); ++i) {
        const QJsonObject v = views.at(i).toObject();
        const int kind = v.value(QStringLiteral("kind")).toInt();
        if (kind != Left && kind != Right)
            continue;
        double param = v.value(QStringLiteral("param")).toDouble();
        if (v.contains(QStringLiteral("ax"))) {
            const QPointF anc(v.value(QStringLiteral("ax")).toDouble() + ref.x() - v.value(QStringLiteral("rx")).toDouble(),
                              v.value(QStringLiteral("ay")).toDouble() + ref.y() - v.value(QStringLiteral("ry")).toDouble());
            param = paramFor(kind, a0, angleDeg, anc);
        }
        SideViewInfo sv;
        sv.viewId = v.value(QStringLiteral("id")).toString();
        sv.kind = kind;
        sv.center = anchorFor(kind, a0, angleDeg, param);
        sv.u = u;
        sv.n = n;
        sv.rMax = rMax;
        out << sv;
    }
    return out;
}

QList<HoleFeature> ProjectionView::holesOf(MyGraphicsScene *scene, const QString &partUuid)
{
    QList<HoleFeature> out;
    const QList<SideViewInfo> svs = sideViews(scene, partUuid);
    if (svs.isEmpty())
        return out;
    const QList<QGraphicsItem *> items = scene->items();
    for (int i = 0; i < items.size(); ++i) {
        const HoleItem *hi = dynamic_cast<const HoleItem *>(items.at(i));
        if (!hi || !hi->isVisible())
            continue;
        const MyHole &mh = hi->holeData();
        const QPointF p = hi->sceneTransform().map(QPointF(mh.position.x, mh.position.y));
        for (int k = 0; k < svs.size(); ++k) {
            const SideViewInfo &sv = svs.at(k);
            const QPointF d = p - sv.center;
            if (std::hypot(d.x(), d.y()) > sv.rMax + 1e-6)
                continue;
            const double du = d.x() * sv.u.x() + d.y() * sv.u.y();
            const double dn = d.x() * sv.n.x() + d.y() * sv.n.y();
            HoleFeature hf;
            hf.hole = mh;
            hf.groupId = hi->data(HOLE_DATA_GROUP_ID).toString();
            hf.viewId = sv.viewId;
            hf.leftSide = (sv.kind == Left);
            hf.y = dn;
            hf.z = hf.leftSide ? du : -du;
            out << hf;
            break;
        }
    }
    return out;
}

bool ProjectionView::drawFrontHoles(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid,
                                    const QString &viewId, const QList<RevolveLoop> &loops, const QPointF &a0,
                                    double angleDeg)
{
    const QList<HoleFeature> holes = holesOf(scene, partUuid);
    const double ang = qDegreesToRadians(angleDeg);
    const QPointF u(std::cos(ang), std::sin(ang)), n(-std::sin(ang), std::cos(ang));
    // 정면 단면이 축 한쪽만 그려져 있으면(반단면) 그쪽 구멍만
    QList<QPainterPath> paths;
    QPointF ref;
    bool hasUp = true, hasLo = true, sym = true;
    if (RevolveProfile::partPaths(scene, partUuid, paths, ref))
        RevolveProfile::sideInfo(paths, a0, a0 + u * 100.0, hasUp, hasLo, sym);
    if (scene->symbolRegistry()->symbolInfo(partUuid).props.value(QStringLiteral("Mirror")).toBool())
        hasUp = hasLo = true;                           // 반대쪽은 미러로 그려짐
    Layer *cur = ctx->layerManager()->getCurrentLayer();
    const QString fallback = cur ? cur->name() : QStringLiteral("0");
    for (int h = 0; h < holes.size(); ++h) {
        const HoleFeature &hf = holes.at(h);
        if (std::fabs(hf.z) > 1e-3)
            continue;                                   // 절단면(z = 0)에 걸린 구멍만 단면으로
        if ((hf.y > 1e-6 && !hasUp) || (hf.y < -1e-6 && !hasLo))
            continue;
        double x0 = 0.0, x1 = 0.0;
        if (!hf.xRange(loops, x0, x1))
            continue;
        // 구멍 단면 도구와 같은 생성기: 입구(그린 쪽 끝면) 중심에서 축 방향으로 파고듦
        const QPointF top = a0 + u * (hf.leftSide ? x0 : x1) + n * hf.y;
        const QPointF dir = hf.leftSide ? u : -u;
        const double thickness = x1 - x0;
        const QList<HoleSectionLine> lines = HoleSectionGenerator::buildLines(hf.hole, top, thickness, dir);
        for (int k = 0; k < lines.size(); ++k) {
            const QString layer = HoleSync::layerForSectionRole(hf.hole, lines.at(k).role, fallback);
            QGraphicsItem *item = ctx->entityFactory()->createLine(lines.at(k).line, AbstractTool::layerPenFor(ctx, layer));
            if (!item)
                continue;
            // 구멍 단면 도구와 같은 태그 → 속성창에서 구멍을 고치면 기존 연동으로 단면도 다시 생성
            item->setData(HOLE_SECTION_GROUP_ID, hf.groupId);
            item->setData(HOLE_SECTION_TOP_X, top.x());
            item->setData(HOLE_SECTION_TOP_Y, top.y());
            item->setData(HOLE_SECTION_DIR_X, dir.x());
            item->setData(HOLE_SECTION_DIR_Y, dir.y());
            item->setData(HOLE_SECTION_MATERIAL_THICKNESS, thickness);
            addItem(ctx, scene, item, layer, viewId);
        }
    }
    return true;
}

int ProjectionView::syncHoleLinks(IAppContext *ctx, MyGraphicsScene *scene)
{
    if (!ctx || !scene)
        return 0;
    int n = 0;
    const QMap<QString, SymbolInfo> all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it)
        if (it.value().type == QLatin1String("MechRevolve") && !viewsOf(it.value()).isEmpty())
            n += regeneratePart(ctx, scene, it.key());
    return n;
}

// ─────────────────────────────────────────────────────────────────────────────
// 측면도 돌출 → 다른 투상도 (호스트 뷰와 같은 뷰 UUID 로 태그)
//   평면·정면: 돌출 프리즘 외형(x0~x1 × 단면 s 범위·각진 꼭짓점 높이). 호스트 외형(|s| ≤ R(x)) 안쪽은 가려짐
//   반대쪽 측면도: 단면 고리를 좌우 뒤집어 그림 (호스트 원 안쪽 부분은 뺌)
// ─────────────────────────────────────────────────────────────────────────────
namespace {
bool sharpYZ(const QPolygonF &p, int k)
{
    const QPointF prev = p.at((k + p.size() - 1) % p.size()), cur = p.at(k), next = p.at((k + 1) % p.size());
    const QPointF d1 = cur - prev, d2 = next - cur;
    const double l1 = std::hypot(d1.x(), d1.y()), l2 = std::hypot(d2.x(), d2.y());
    if (l1 < 1e-9 || l2 < 1e-9)
        return false;
    return (d1.x() * d2.x() + d1.y() * d2.y()) / (l1 * l2) < std::cos(qDegreesToRadians(15.0));
}

double hostR(const QList<RevolveLoop> &loops, double x)
{
    return std::max(outerR(loops, x - 1e-4), outerR(loops, x + 1e-4));
}
} // namespace

void ProjectionView::drawExtrudes(IAppContext *ctx, MyGraphicsScene *scene, const QString &hostUuid,
                                  const QString &viewId, int kind, double param, bool showHidden,
                                  const QList<RevolveLoop> &loops, const QPointF &a0, double angleDeg)
{
    const QList<QString> ids = ExtrudeFeature::attachedTo(scene, hostUuid);
    if (ids.isEmpty())
        return;
    const double ang = qDegreesToRadians(angleDeg);
    const QPointF u(std::cos(ang), std::sin(ang)), n(-std::sin(ang), std::cos(ang));
    Layer *cur = ctx->layerManager()->getCurrentLayer();
    const QString visLayer = cur ? cur->name() : QStringLiteral("0");
    const QString hidLayer = showHidden ? hiddenLayer(ctx) : visLayer;
    const QPen visPen = AbstractTool::layerPenFor(ctx, visLayer);
    const QPen hidPen = AbstractTool::layerPenFor(ctx, hidLayer);
    QList<double> hostBreaks;
    for (int i = 0; i < loops.size(); ++i)
        for (int k = 0; k < loops.at(i).pts.size(); ++k)
            hostBreaks << loops.at(i).pts.at(k).x();
    std::sort(hostBreaks.begin(), hostBreaks.end());

    for (int e = 0; e < ids.size(); ++e) {
        ExtrudeInfo ei;
        if (!ExtrudeFeature::resolve(scene, ids.at(e), ei))
            continue;
        QList<QLineF> vis, hid;

        if (kind == Left || kind == Right) {
            if (ei.viewKind == kind)
                continue;                                   // 사용자가 그린 측면도
            const QPointF c = anchorFor(kind, a0, angleDeg, param);
            double rCover = 0.0;                            // 이 범위에서 호스트 가장 큰 반지름
            for (double x = ei.x0; x <= ei.x1 + 1e-9; x += std::max(0.1, (ei.x1 - ei.x0) / 20.0))
                rCover = std::max(rCover, hostR(loops, x));
            const bool left = (kind == Left);
            for (int r = 0; r < ei.rings.size(); ++r) {
                const QPolygonF &p = ei.rings.at(r);
                for (int k = 0; k < p.size(); ++k) {
                    const QPointF a = p.at(k), b = p.at((k + 1) % p.size());
                    const QPointF m = (a + b) * 0.5;
                    if (std::hypot(m.x(), m.y()) <= rCover + 1e-3)
                        continue;                           // 호스트 원과 겹치는 부분(투상 원이 이미 있음)
                    vis << QLineF(c + u * (left ? a.y() : -a.y()) + n * a.x(), c + u * (left ? b.y() : -b.y()) + n * b.x());
                }
            }
        } else {
            // 정면: s = y, 평면: s = −z (평면도 위쪽 = 뒤)
            const QPointF base = a0 + n * param;
            double smin = 1e300, smax = -1e300;
            QList<double> svals;
            for (int r = 0; r < ei.rings.size(); ++r) {
                const QPolygonF &p = ei.rings.at(r);
                for (int k = 0; k < p.size(); ++k) {
                    const double sv = (kind == Front) ? p.at(k).x() : -p.at(k).y();
                    smin = std::min(smin, sv);
                    smax = std::max(smax, sv);
                    if (sharpYZ(p, k))
                        svals << sv;
                }
            }
            svals << smin << smax;
            std::sort(svals.begin(), svals.end());
            QList<double> uniq;
            for (int i = 0; i < svals.size(); ++i)
                if (uniq.isEmpty() || std::fabs(svals.at(i) - uniq.last()) > 1e-4)
                    uniq << svals.at(i);
            // 가로선(x0~x1, 높이 s): 호스트 꼭짓점 x 로 잘라 조각별로 가려짐 판정
            for (int i = 0; i < uniq.size(); ++i) {
                const double sv = uniq.at(i);
                QList<double> cuts;
                cuts << ei.x0;
                for (int t = 0; t < hostBreaks.size(); ++t)
                    if (hostBreaks.at(t) > ei.x0 + 1e-6 && hostBreaks.at(t) < ei.x1 - 1e-6)
                        cuts << hostBreaks.at(t);
                cuts << ei.x1;
                for (int t = 0; t + 1 < cuts.size(); ++t) {
                    const double xa = cuts.at(t), xb = cuts.at(t + 1);
                    const double R = outerR(loops, (xa + xb) * 0.5);
                    if (std::fabs(std::fabs(sv) - R) < 1e-4)
                        continue;                       // 호스트 외형선과 겹침 (호스트가 이미 그림)
                    const bool covered = std::fabs(sv) < R;
                    (covered ? hid : vis) << QLineF(base + u * xa + n * sv, base + u * xb + n * sv);
                }
            }
            // 세로선(x0, x1): |s| < R(x) 인 부분은 가려짐
            const double xs[2] = {ei.x0, ei.x1};
            for (int j = 0; j < 2; ++j) {
                const double R = hostR(loops, xs[j]);
                bool onHostFace = false;                // 호스트 끝면·턱면과 같은 x → 가려진 부분은 호스트 선과 겹침
                for (int t = 0; t < hostBreaks.size() && !onHostFace; ++t)
                    onHostFace = std::fabs(hostBreaks.at(t) - xs[j]) < 1e-6;
                QList<double> cs;
                cs << smin;
                if (-R > smin && -R < smax)
                    cs << -R;
                if (R > smin && R < smax)
                    cs << R;
                cs << smax;
                for (int t = 0; t + 1 < cs.size(); ++t) {
                    const double sm = (cs.at(t) + cs.at(t + 1)) * 0.5;
                    const bool covered = std::fabs(sm) < R - 1e-6;
                    if (covered && onHostFace)
                        continue;
                    (covered ? hid : vis) << QLineF(base + u * xs[j] + n * cs.at(t), base + u * xs[j] + n * cs.at(t + 1));
                }
            }
        }
        mergeCollinear(vis);
        mergeCollinear(hid);
        EntityFactory *ef = ctx->entityFactory();
        for (int i = 0; i < vis.size(); ++i) {
            QGraphicsItem *it = ef->createLine(vis.at(i), visPen);
            if (it)
                it->setData(MECH_EXTRUDE_ID, ids.at(e));
            addItem(ctx, scene, it, visLayer, viewId);
        }
        if (showHidden)
            for (int i = 0; i < hid.size(); ++i) {
                QGraphicsItem *it = ef->createLine(hid.at(i), hidPen);
                if (it)
                    it->setData(MECH_EXTRUDE_ID, ids.at(e));
                addItem(ctx, scene, it, hidLayer, viewId);
            }
    }
}

void ProjectionView::setMirror(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid, bool mirror)
{
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    if (!reg->hasSymbol(partUuid))
        return;
    const SymbolInfo oldInfo = reg->symbolInfo(partUuid);
    SymbolInfo info = oldInfo;
    info.props[QStringLiteral("Mirror")] = mirror;
    if (ctx && ctx->undoStack())
        ctx->undoStack()->push(new UpdateRegistryCommand(reg, partUuid, oldInfo, info));
    else
        reg->registerSymbol(partUuid, info);
    if (mirror)
        ensureFrontView(ctx, scene, partUuid);
    regeneratePart(ctx, scene, partUuid);
}

void ProjectionView::drawFrontMirror(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid,
                                     const QString &viewId, const QPointF &a0, double angleDeg)
{
    const SymbolInfo info = scene->symbolRegistry()->symbolInfo(partUuid);
    if (info.type != QLatin1String("MechRevolve") || !info.props.value(QStringLiteral("Mirror")).toBool())
        return;
    QList<QPainterPath> paths;
    QPointF ref;
    if (!RevolveProfile::partPaths(scene, partUuid, paths, ref))
        return;
    const double ang = qDegreesToRadians(angleDeg);
    const QPointF u(std::cos(ang), std::sin(ang)), n(-std::sin(ang), std::cos(ang));
    bool hasUp = false, hasLo = false, sym = true;
    RevolveProfile::sideInfo(paths, a0, a0 + u * 100.0, hasUp, hasLo, sym);
    if (hasUp == hasLo)
        return;                                         // 이미 양쪽이 그려져 있음 (또는 없음)
    const int side = hasUp ? RevolveProfile::SideUpper : RevolveProfile::SideLower;
    const QList<RevolveLoop> loops = RevolveProfile::sectionLoops(paths, a0, a0 + u * 100.0, side);
    const double toward = hasUp ? -1.0 : 1.0;           // 그릴 쪽 (위가 원본이면 아래로)

    Layer *cur = ctx->layerManager()->getCurrentLayer();
    const QString visLayer = cur ? cur->name() : QStringLiteral("0");
    const QPen visPen = AbstractTool::layerPenFor(ctx, visLayer);
    EntityFactory *ef = ctx->entityFactory();

    // 외형선 (축 위의 변은 대칭 이음매라 생략)
    QList<QLineF> lines;
    QPainterPath hatchPath;
    hatchPath.setFillRule(Qt::OddEvenFill);
    for (int i = 0; i < loops.size(); ++i) {
        const QPolygonF &p = loops.at(i).pts;
        QPolygonF dp;
        for (int k = 0; k < p.size(); ++k)
            dp << a0 + u * p.at(k).x() + n * (toward * p.at(k).y());
        dp << dp.first();
        hatchPath.addPolygon(dp);
        for (int k = 0; k < p.size(); ++k) {
            const QPointF a = p.at(k), b = p.at((k + 1) % p.size());
            if (a.y() < 1e-6 && b.y() < 1e-6)
                continue;
            lines << QLineF(dp.at(k), dp.at(k + 1));
        }
    }
    mergeCollinear(lines);
    for (int i = 0; i < lines.size(); ++i)
        addItem(ctx, scene, ef->createLine(lines.at(i), visPen), visLayer, viewId);

    // 해칭 방식이면 원래 해칭과 같은 무늬로
    const QList<QGraphicsItem *> items = scene->items();
    for (int i = 0; i < items.size(); ++i) {
        const HatchItem *h = dynamic_cast<const HatchItem *>(items.at(i));
        if (!h || h->data(MECH_DATA_PART_ID).toString() != partUuid)
            continue;
        const QMap<QString, QVariant> hp = h->getProperties();
        const QString layer = h->layerName();
        QGraphicsItem *hi = ef->createHatch(hatchPath, hp.value(QStringLiteral("Pattern")).toString(),
                                            hp.value(QStringLiteral("Scale"), 1.0).toDouble(),
                                            hp.value(QStringLiteral("Angle"), 0.0).toDouble(),
                                            AbstractTool::layerPenFor(ctx, layer));
        addItem(ctx, scene, hi, layer, viewId);
        break;
    }
}
