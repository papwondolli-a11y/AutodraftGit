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
#include "plateview.h"
#include "scenesymbolregistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
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

// 구멍을 받는 회전 부품 (측면도 구멍R·구멍P, 정면 구멍 단면 → 3D·투상도 연동): 단면 회전체·축
bool holeHost(const QString &type)
{
    return type == QLatin1String("MechRevolve") || type == QLatin1String("MechShaft");
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

int ProjectionView::faceKind(double normalDeg)
{
    double a = std::fmod(normalDeg, 360.0);
    if (a < 0.0)
        a += 360.0;
    int k = int(std::lround(a * 1000.0));
    if (k >= 360000)
        k -= 360000;
    return FaceKindBase + k;
}

bool ProjectionView::isFaceKind(int kind)
{
    return kind >= FaceKindBase && kind < FaceKindBase + 360000;
}

double ProjectionView::faceAngle(int kind)
{
    return (kind - FaceKindBase) / 1000.0;
}

int ProjectionView::revFaceKind(double phiDeg)
{
    double a = std::fmod(phiDeg, 360.0);
    if (a < 0.0)
        a += 360.0;
    int k = int(std::lround(a * 1000.0));
    if (k >= 360000)
        k -= 360000;
    return RevFaceKindBase + k;
}

bool ProjectionView::isRevFaceKind(int kind)
{
    return kind >= RevFaceKindBase && kind < RevFaceKindBase + 360000;
}

double ProjectionView::revFaceAngle(int kind)
{
    return (kind - RevFaceKindBase) / 1000.0;
}

QString ProjectionView::kindName(int kind)
{
    if (isFaceKind(kind) || isRevFaceKind(kind))
        return QStringLiteral("보조 투상도");
    if (kind == Left)
        return QStringLiteral("좌측면도");
    if (kind == Right)
        return QStringLiteral("우측면도");
    if (kind == Front)
        return QStringLiteral("정면 구멍선");
    return QStringLiteral("평면도(외형)");
}

bool ProjectionView::partFrame(MyGraphicsScene *scene, const QString &uuid, QPointF &a0, double &angleDeg,
                               QString &name, QPointF *ref)
{
    if (PlateView::isPlate(scene, uuid)) {
        ExtrudeInfo ei;
        QPointF r;
        if (!PlateView::frame(scene, uuid, ei, a0, r))
            return false;
        angleDeg = 0.0;
        name = ei.name;
        if (ref)
            *ref = r;
        return true;
    }
    QList<RevolveLoop> loops;
    return MechModel3D::revolveSection(scene, uuid, loops, a0, angleDeg, name, ref);
}

ProjDrawing ProjectionView::buildPart(MyGraphicsScene *scene, const QString &uuid, int kind, double param,
                                      bool showHidden)
{
    if (PlateView::isPlate(scene, uuid)) {
        ExtrudeInfo ei;
        QPointF a0, ref;
        if (!PlateView::frame(scene, uuid, ei, a0, ref))
            return ProjDrawing();
        return PlateView::build(ei, kind, a0, param, showHidden, PlateView::holes(scene, uuid), QString(),
                                PlateView::bosses(scene, uuid));
    }
    QList<RevolveLoop> loops;
    QPointF a0;
    double angleDeg = 0.0;
    QString name;
    if (!MechModel3D::revolveSection(scene, uuid, loops, a0, angleDeg, name))
        return ProjDrawing();
    return build(loops, kind, a0, angleDeg, param, showHidden, holesOf(scene, uuid),
                 sectionHolesOf(scene, uuid, loops, a0, angleDeg));
}

double ProjectionView::paramFor(int kind, const QPointF &a0, double angleDeg, const QPointF &p)
{
    if (isFaceKind(kind)) {                             // 보조 투상도: 법선 방향 거리
        const double fa = qDegreesToRadians(faceAngle(kind));
        const QPointF d = p - a0;
        return d.x() * std::cos(fa) + d.y() * std::sin(fa);
    }
    const double ang = qDegreesToRadians(angleDeg);
    const QPointF u(std::cos(ang), std::sin(ang)), n(-std::sin(ang), std::cos(ang));
    const QPointF d = p - a0;
    return (kind == Top || kind == Front || isRevFaceKind(kind)) ? (d.x() * n.x() + d.y() * n.y())
                                                                 : (d.x() * u.x() + d.y() * u.y());
}

QPointF ProjectionView::anchorFor(int kind, const QPointF &a0, double angleDeg, double param)
{
    if (isFaceKind(kind)) {
        const double fa = qDegreesToRadians(faceAngle(kind));
        return a0 + QPointF(std::cos(fa), std::sin(fa)) * param;
    }
    const double ang = qDegreesToRadians(angleDeg);
    const QPointF u(std::cos(ang), std::sin(ang)), n(-std::sin(ang), std::cos(ang));
    return a0 + ((kind == Top || kind == Front || isRevFaceKind(kind)) ? n : u) * param;
}

ProjDrawing ProjectionView::build(const QList<RevolveLoop> &loops, int kind, const QPointF &a0, double angleDeg,
                                  double param, bool showHidden, const QList<HoleFeature> &holes,
                                  const QList<SectionHole> &secHoles)
{
    ProjDrawing d = buildAll(loops, kind, a0, angleDeg, param, holes, secHoles);
    if (!showHidden) {
        d.hidden.clear();
        d.hiddenCircles.clear();
    }
    return d;
}

ProjDrawing ProjectionView::buildAll(const QList<RevolveLoop> &loops, int kind, const QPointF &a0, double angleDeg,
                                     double param, const QList<HoleFeature> &holes,
                                     const QList<SectionHole> &secHoles)
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
        // 정면에 구멍 단면 도구로 그린 구멍 (절단면 z = 0 위)
        for (int h = 0; h < secHoles.size(); ++h) {
            const SectionHole &sh = secHoles.at(h);
            const double r = sh.hole.diameter * 0.5;
            if (sh.axial) {
                // 축 방향: 이 쪽 끝면에서 들어간 구멍이거나 관통이면 실선 원, 아니면 숨은선 원
                const bool open = sh.hole.throughHole || (leftSide ? sh.dirSign > 0.0 : sh.dirSign < 0.0);
                (open ? out.visibleCircles : out.hiddenCircles) << ProjCircle{c + n * sh.s, r};
            } else {
                // 반경 방향: 측면에서는 재료 속 → 숨은선 두 줄 (입구 높이 s 에서 드릴 깊이만큼 축 쪽으로)
                HoleFeature tmp;
                tmp.hole = sh.hole;
                const double depth = std::min(tmp.drillDepth(sh.thickness), std::fabs(sh.s) + rMax);
                const double s1 = sh.s + sh.dirSign * depth;
                for (int sgn = -1; sgn <= 1; sgn += 2)
                    out.hidden << QLineF(c + u * (sgn * r) + n * sh.s, c + u * (sgn * r) + n * s1);
            }
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
    // 뷰 위쪽 방향 e (로컬 y, z) · 보는 쪽 dn: 평면도 = 위(+y)에서 봄 → e = (0, −1) (뷰 위쪽 = 뒤 = −z)
    //   회전체 보조 투상도(φ): dn = (cos φ, sin φ), e = (sin φ, −cos φ)
    double ey = 0.0, ez = -1.0, dny = 1.0;
    if (isRevFaceKind(kind)) {
        const double ph = qDegreesToRadians(revFaceAngle(kind));
        ey = std::sin(ph);
        ez = -std::cos(ph);
        dny = std::cos(ph);
    }
    for (int h = 0; h < holes.size(); ++h) {
        const HoleFeature &hf = holes.at(h);
        double hx0 = 0.0, hx1 = 0.0;
        if (!hf.xRange(loops, hx0, hx1))
            continue;
        const double off = hf.y * ey + hf.z * ez;           // 평면도면 −z
        for (int sgn = -1; sgn <= 1; sgn += 2)
            out.hidden << QLineF(base + u * hx0 + n * (off + sgn * hf.hole.diameter * 0.5),
                                 base + u * hx1 + n * (off + sgn * hf.hole.diameter * 0.5));
    }
    // 정면에 구멍 단면 도구로 그린 구멍: 평면도는 위(정면 +s 쪽)에서 내려다본 모습
    for (int h = 0; h < secHoles.size(); ++h) {
        const SectionHole &sh = secHoles.at(h);
        const double r = sh.hole.diameter * 0.5;
        HoleFeature tmp;
        tmp.hole = sh.hole;
        if (sh.axial) {
            // 축 방향: 재료 속 → 숨은선 두 줄 (입구 x 에서 드릴 깊이만큼). 절단면(z = 0) 위라 뷰 높이 = s·ey
            const double x1 = sh.x + sh.dirSign * tmp.drillDepth(sh.thickness);
            const double s0 = sh.s * ey;
            for (int sgn = -1; sgn <= 1; sgn += 2)
                out.hidden << QLineF(base + u * sh.x + n * (s0 + sgn * r), base + u * x1 + n * (s0 + sgn * r));
        } else if (std::fabs(ey) < 1e-6) {
            // 반경 방향(y 축 방향)을 정면으로 봄: 보는 쪽에서 들어가면 보이는 원, 아니면 숨은선 원 (관통이면 보임)
            const bool open = sh.s * dny > 0.0 || sh.hole.throughHole;
            (open ? out.visibleCircles : out.hiddenCircles) << ProjCircle{base + u * sh.x, r};
        } else {
            // 비스듬히 봄: 재료 속 숨은선 두 줄 (입구 높이 → 드릴 깊이만큼 축 쪽)
            const double s1 = sh.s + sh.dirSign * tmp.drillDepth(sh.thickness);
            for (int sgn = -1; sgn <= 1; sgn += 2)
                out.hidden << QLineF(base + u * (sh.x + sgn * r) + n * (sh.s * ey),
                                     base + u * (sh.x + sgn * r) + n * (s1 * ey));
        }
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
    ProjDrawing d;
    const bool plate = PlateView::isPlate(scene, partUuid);
    if (plate) {
        // 판(단독 돌출): 측면·평면 외형 + 구멍, 정면 = 옆면 구멍 숨은선
        ExtrudeInfo ei;
        QPointF ref;
        if (!PlateView::frame(scene, partUuid, ei, a0, ref)) {
            if (error)
                *error = QStringLiteral("판의 공간을 찾지 못했습니다.");
            return false;
        }
        if (kind == Front)
            showHidden = true;                          // 옆면 구멍은 정면에 숨은선으로 (관례)
        d = PlateView::build(ei, kind, a0, param, showHidden, PlateView::holes(scene, partUuid), viewId,
                             PlateView::bosses(scene, partUuid));
        if (kind == Front && d.isEmpty())
            return true;
    } else if (!MechModel3D::revolveSection(scene, partUuid, loops, a0, angleDeg, name)) {
        if (error)
            *error = QStringLiteral("이 부품은 투상도를 만들 수 있는 회전체가 아닙니다.");
        return false;
    } else {
        d = build(loops, kind, a0, angleDeg, param, showHidden, holesOf(scene, partUuid),
                  sectionHolesOf(scene, partUuid, loops, a0, angleDeg));
        if (isRevFaceKind(kind)) {
            // 받침대 등 붙은 돌출에 가려지는 회전체 외형 → 숨은선
            occludeByExtrudes(scene, partUuid, kind, param, loops, a0, angleDeg, d);
            if (!showHidden) {
                d.hidden.clear();
                d.hiddenCircles.clear();
            }
        }
    }
    if (kind == Front && !plate) {
        // 구멍 단면 자리는 해칭에서 뺀다 (원본 해칭·미러 해칭 모두). 3D 단면은 선 영역이라 영향 없음
        const QPainterPath holeArea = frontHoleArea(scene, partUuid, loops, a0, angleDeg);
        drawFrontMirror(ctx, scene, partUuid, viewId, a0, angleDeg, holeArea);
        drawFrontHoles(ctx, scene, partUuid, viewId, loops, a0, angleDeg);
        updatePartHatch(ctx, scene, partUuid, holeArea);
        drawExtrudes(ctx, scene, partUuid, viewId, kind, param, showHidden, loops, a0, angleDeg);
        drawAuxHoles(ctx, scene, partUuid, viewId, kind, param, showHidden, a0, angleDeg);
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
    if (!plate) {
        drawExtrudes(ctx, scene, partUuid, viewId, kind, param, showHidden, loops, a0, angleDeg);
        drawAuxHoles(ctx, scene, partUuid, viewId, kind, param, showHidden, a0, angleDeg);
    }
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
    QPointF a0, ref;
    double angleDeg = 0.0;
    QString name;
    QJsonObject v;
    if (partFrame(scene, partUuid, a0, angleDeg, name, &ref)) {
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
    bool needFront = !holes.isEmpty() || !auxHolesOf(scene, partUuid).isEmpty();
    if (PlateView::isPlate(scene, partUuid)) {
        // 판: 측면·평면도에 놓은 옆면 구멍이 있으면 정면에 숨은선 → 정면 기록 필요
        const QList<PlateHole> phs = PlateView::holes(scene, partUuid);
        for (int i = 0; i < phs.size() && !needFront; ++i)
            needFront = !phs.at(i).fromSection && std::fabs(phs.at(i).dir.z()) < 0.5;
        if (!needFront)
            needFront = !PlateView::bosses(scene, partUuid).isEmpty();   // 돌기 → 정면에 돌기 외형
    } else if (!needFront) {   // 구멍 단면 도구로 정면에 직접 그린 구멍도 정면 뷰(해칭 빼기)가 필요
        QList<RevolveLoop> loops;
        QPointF a0;
        double angleDeg = 0.0;
        QString name;
        if (MechModel3D::revolveSection(scene, partUuid, loops, a0, angleDeg, name))
            needFront = !sectionHolesOf(scene, partUuid, loops, a0, angleDeg).isEmpty();
    }
    if (needFront)
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
        QPointF a0, ref;
        double angleDeg = 0.0;
        QString name;
        if (v.contains(QStringLiteral("ax")) && partFrame(scene, partUuid, a0, angleDeg, name, &ref)) {
            const QPointF anc(v.value(QStringLiteral("ax")).toDouble() + ref.x() - v.value(QStringLiteral("rx")).toDouble(),
                              v.value(QStringLiteral("ay")).toDouble() + ref.y() - v.value(QStringLiteral("ry")).toDouble());
            param = paramFor(kind, a0, angleDeg, anc);
        }
        if (v.value(QStringLiteral("concealed")).toBool() && kind != Front)
            continue;                                   // 숨긴 투상도: 기록만 남기고 그리지 않음
        if (drawView(ctx, scene, partUuid, viewId, kind, param, v.value(QStringLiteral("hidden")).toBool(), nullptr))
            ++count;
    }
    return count;
}

QSet<QString> ProjectionView::viewIdsOf(const QList<QGraphicsItem *> &items)
{
    QSet<QString> ids;
    for (int i = 0; i < items.size(); ++i) {
        if (!items.at(i))
            continue;
        const QString v = items.at(i)->data(MECH_VIEW_ID).toString();
        if (!v.isEmpty())
            ids.insert(v);
        // 투상도에 그린 돌출 선: "ext:<돌출 UUID>|<뷰 UUID>" — 지운 뒤 돌출 기록도 지울지 판단 (syncAfterErase)
        const QString e = items.at(i)->data(MECH_EXTRUDE_ID).toString();
        if (!e.isEmpty())
            ids.insert(QStringLiteral("ext:") + e + QLatin1Char('|') + v);
        // 구멍R·구멍P 객체를 지움 → 부품 투상도(정면 구멍 단면·다른 측면도 원)를 다시 그려야 함
        if (dynamic_cast<const HoleItem *>(items.at(i)))
            ids.insert(QStringLiteral("hole:"));
    }
    return ids;
}

int ProjectionView::syncAfterErase(IAppContext *ctx, MyGraphicsScene *scene, const QSet<QString> &touchedViews)
{
    if (!ctx || !scene)
        return 0;
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    // 아직 선이 남아 있는 뷰
    QSet<QString> alive;
    const QList<QGraphicsItem *> items = scene->items();
    for (int i = 0; i < items.size(); ++i) {
        const QString v = items.at(i)->data(MECH_VIEW_ID).toString();
        if (!v.isEmpty())
            alive.insert(v);
    }
    QSet<QString> regen;
    const QMap<QString, SymbolInfo> all = reg->allSymbols();   // 복사 (도는 중에 기록을 고침)
    if (!touchedViews.isEmpty()) {
        for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
            const QJsonArray views = viewsOf(it.value());
            if (views.isEmpty())
                continue;
            QJsonArray kept;
            bool changed = false;
            for (int i = 0; i < views.size(); ++i) {
                const QJsonObject v = views.at(i).toObject();
                const QString id = v.value(QStringLiteral("id")).toString();
                if (touchedViews.contains(id) && !alive.contains(id) && v.value(QStringLiteral("kind")).toInt() != Front) {
                    changed = true;                     // 뷰 선을 모두 지움 = 뷰 삭제
                    continue;
                }
                kept.append(v);
            }
            if (!changed)
                continue;
            SymbolInfo info = it.value();
            info.props[QStringLiteral("Views")] = QString::fromUtf8(QJsonDocument(kept).toJson(QJsonDocument::Compact));
            if (ctx->undoStack())
                ctx->undoStack()->push(new UpdateRegistryCommand(reg, it.key(), it.value(), info));
            else
                reg->registerSymbol(it.key(), info);
            regen.insert(it.key());
        }
    }
    // 투상도의 돌출 선을 모두 지움 = 그 돌출을 지운 것 → 돌출 기록도 삭제 (남겨 두면 다시 그릴 때 되살아남)
    //   단, 뷰를 통째로 지운 경우(그 뷰에 남은 선이 없음)는 뷰만 지운 것이므로 돌출은 그대로 둔다
    {
        QHash<QString, QSet<QString>> extViews;         // 돌출 UUID → 지운 선이 있던 뷰들
        for (QSet<QString>::const_iterator t = touchedViews.constBegin(); t != touchedViews.constEnd(); ++t) {
            if (!t->startsWith(QLatin1String("ext:")))
                continue;
            const QString body = t->mid(4);
            const int bar = body.indexOf(QLatin1Char('|'));
            extViews[bar < 0 ? body : body.left(bar)].insert(bar < 0 ? QString() : body.mid(bar + 1));
        }
        if (!extViews.isEmpty()) {
            QSet<QString> extAlive;                     // 아직 선이 남은 돌출
            for (int i = 0; i < items.size(); ++i) {
                const QString e = items.at(i)->data(MECH_EXTRUDE_ID).toString();
                if (!e.isEmpty())
                    extAlive.insert(e);
            }
            for (QHash<QString, QSet<QString>>::const_iterator e = extViews.constBegin(); e != extViews.constEnd(); ++e) {
                if (extAlive.contains(e.key()) || !reg->hasSymbol(e.key()))
                    continue;
                bool viewKept = false;
                for (QSet<QString>::const_iterator v = e.value().constBegin(); v != e.value().constEnd() && !viewKept; ++v)
                    viewKept = alive.contains(*v);
                if (!viewKept)
                    continue;
                const SymbolInfo ei = reg->symbolInfo(e.key());
                if (ei.type != QLatin1String("MechExtrude"))
                    continue;
                if (ctx->undoStack())
                    ctx->undoStack()->push(new UnregisterSymbolCommand(reg, e.key(), ei));
                else
                    reg->unregisterSymbol(e.key());
                regen.insert(ei.props.value(QStringLiteral("Host")).toString());
            }
        }
    }
    // 돌기·파내기가 붙은 부품 (스케치·뷰를 지우면 다른 투상도의 선도 바뀜)
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        const QMap<QString, QVariant> &p = it.value().props;
        if (it.value().type != QLatin1String("MechExtrude"))
            continue;
        if (p.value(QStringLiteral("PlateBoss")).toBool() || p.value(QStringLiteral("PlateFront")).toBool() ||
            p.value(QStringLiteral("Cut")).toBool())
            regen.insert(p.value(QStringLiteral("Host")).toString());
    }
    int n = 0;
    for (QSet<QString>::const_iterator it = regen.constBegin(); it != regen.constEnd(); ++it)
        if (reg->hasSymbol(*it) && !viewsOf(reg->symbolInfo(*it)).isEmpty()) {
            regeneratePart(ctx, scene, *it);
            ++n;
        }
    // 구멍을 지웠으면 구멍을 받는 부품들을 다시 그림 — 지운 구멍의 정면 단면 선이 남지 않게
    //   (남아 있으면 구멍이 있는 것처럼 보이다가 나중에 다시 그릴 때 갑자기 사라짐)
    if (touchedViews.contains(QStringLiteral("hole:")))
        n += syncHoleLinks(ctx, scene);
    return n;
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
    // 판(단독 돌출): 공간 안을 클릭
    return PlateView::plateAt(scene, pos);
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
    if (!holeHost(info.type))                           // 단면 회전체·축 (구멍을 받을 수 있는 회전 부품)
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
        sv.concealed = v.value(QStringLiteral("concealed")).toBool();
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
        // 숨긴 투상도 안이라 화면에서만 숨긴 구멍은 계속 부품 구멍으로 셈
        if (!hi || (!hi->isVisible() && !hi->data(MECH_CONCEALED_HOLE).toBool()))
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
    Layer *cur = ctx->layerManager()->getCurrentLayer();
    const QString fallback = cur ? cur->name() : QStringLiteral("0");
    const QList<FrontHole> fhs = frontHoles(scene, partUuid, loops, a0, angleDeg);
    for (int h = 0; h < fhs.size(); ++h) {
        const HoleFeature &hf = fhs.at(h).hf;
        const QPointF top = fhs.at(h).top;
        const QPointF dir = fhs.at(h).dir;
        const double thickness = fhs.at(h).thickness;
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

QList<ProjectionView::FrontHole> ProjectionView::frontHoles(MyGraphicsScene *scene, const QString &partUuid,
                                                            const QList<RevolveLoop> &loops, const QPointF &a0,
                                                            double angleDeg)
{
    QList<FrontHole> out;
    const QList<HoleFeature> holes = holesOf(scene, partUuid);
    if (holes.isEmpty())
        return out;
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
    for (int h = 0; h < holes.size(); ++h) {
        const HoleFeature &hf = holes.at(h);
        if (std::fabs(hf.z) > 1e-3)
            continue;                                   // 절단면(z = 0)에 걸린 구멍만 단면으로
        if ((hf.y > 1e-6 && !hasUp) || (hf.y < -1e-6 && !hasLo))
            continue;
        // 재료 전체 범위 — 깊이는 생성기가 구멍 스펙(관통/깊이/탭 깊이)으로 정한다
        //   (막힌 탭은 depth 필드가 0 이어도 나사 깊이로 파므로 xRange 로 거르면 안 됨)
        double x0 = 0.0, x1 = 0.0;
        if (!hf.materialRange(loops, x0, x1))
            continue;
        FrontHole fh;
        fh.hf = hf;
        // 구멍 단면 도구와 같은 생성기: 입구(그린 쪽 끝면) 중심에서 축 방향으로 파고듦
        fh.top = a0 + u * (hf.leftSide ? x0 : x1) + n * hf.y;
        fh.dir = hf.leftSide ? u : -u;
        fh.thickness = x1 - x0;
        out << fh;
    }
    return out;
}

QPainterPath ProjectionView::frontHoleArea(MyGraphicsScene *scene, const QString &partUuid,
                                           const QList<RevolveLoop> &loops, const QPointF &a0, double angleDeg)
{
    QPainterPath area;
    const QList<FrontHole> fhs = frontHoles(scene, partUuid, loops, a0, angleDeg);
    for (int i = 0; i < fhs.size(); ++i) {
        const QPainterPath p = fhs.at(i).hf.frontArea(fhs.at(i).top, fhs.at(i).dir, fhs.at(i).thickness);
        area = area.isEmpty() ? p : area.united(p);
    }
    // 구멍 단면 도구로 직접 그린 구멍도 해칭에서 뺀다
    const double ang = qDegreesToRadians(angleDeg);
    const QPointF u(std::cos(ang), std::sin(ang)), n(-std::sin(ang), std::cos(ang));
    const QList<SectionHole> shs = sectionHolesOf(scene, partUuid, loops, a0, angleDeg);
    for (int i = 0; i < shs.size(); ++i) {
        const SectionHole &sh = shs.at(i);
        HoleFeature tmp;
        tmp.hole = sh.hole;
        const QPointF top = a0 + u * sh.x + n * sh.s;
        const QPointF dir = sh.axial ? u * sh.dirSign : n * sh.dirSign;
        const QPainterPath p = tmp.frontArea(top, dir, sh.thickness);
        area = area.isEmpty() ? p : area.united(p);
    }
    return area;
}

QList<SectionHole> ProjectionView::sectionHolesOf(MyGraphicsScene *scene, const QString &partUuid,
                                                  const QList<RevolveLoop> &loops, const QPointF &a0,
                                                  double angleDeg)
{
    QList<SectionHole> out;
    if (!scene || loops.isEmpty())
        return out;
    const double ang = qDegreesToRadians(angleDeg);
    const QPointF u(std::cos(ang), std::sin(ang)), n(-std::sin(ang), std::cos(ang));
    // 회전 단면 (x = 축 방향, r = 반지름) — 점이 재료 안에 있는지 판정용
    QPainterPath sec;
    sec.setFillRule(Qt::OddEvenFill);
    for (int i = 0; i < loops.size(); ++i) {
        QPolygonF pg = loops.at(i).pts;
        if (!pg.isEmpty())
            pg << pg.first();
        sec.addPolygon(pg);
    }
    const QRectF secBox = sec.boundingRect();
    // 원본 구멍 스펙 (HOLE_DATA_GROUP_ID → HoleItem)
    QHash<QString, MyHole> specs;
    const QList<QGraphicsItem *> items = scene->items();
    for (int i = 0; i < items.size(); ++i) {
        const HoleItem *hi = dynamic_cast<const HoleItem *>(items.at(i));
        if (!hi)
            continue;
        const QString gid = hi->data(HOLE_DATA_GROUP_ID).toString();
        if (!gid.isEmpty() && !specs.contains(gid))
            specs.insert(gid, hi->holeData());
    }
    QSet<QString> seen;
    for (int i = 0; i < items.size(); ++i) {
        const QGraphicsItem *gi = items.at(i);
        const QString gid = gi->data(HOLE_SECTION_GROUP_ID).toString();
        if (gid.isEmpty() || !gi->data(MECH_VIEW_ID).toString().isEmpty() || !gi->isVisible())
            continue;                                   // 측면도 연동이 그린 단면은 이미 측면 구멍으로 반영됨
        if (!specs.contains(gid))
            continue;
        const QPointF top(gi->data(HOLE_SECTION_TOP_X).toDouble(), gi->data(HOLE_SECTION_TOP_Y).toDouble());
        QPointF dir(gi->data(HOLE_SECTION_DIR_X).toDouble(), gi->data(HOLE_SECTION_DIR_Y).toDouble());
        const double dl = std::hypot(dir.x(), dir.y());
        if (dl < 1e-9)
            continue;
        dir /= dl;
        // 한 구멍 단면 = 같은 원본·같은 입구·같은 방향의 선 여러 개 → 한 번만
        const QString key = QStringLiteral("%1|%2|%3|%4|%5").arg(gid).arg(top.x(), 0, 'f', 4).arg(top.y(), 0, 'f', 4)
                                .arg(dir.x(), 0, 'f', 4).arg(dir.y(), 0, 'f', 4);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        SectionHole sh;
        sh.hole = specs.value(gid);
        sh.groupId = gid;
        sh.thickness = gi->data(HOLE_SECTION_MATERIAL_THICKNESS).toDouble();
        const QPointF d = top - a0;
        sh.x = d.x() * u.x() + d.y() * u.y();
        sh.s = d.x() * n.x() + d.y() * n.y();
        const double du = dir.x() * u.x() + dir.y() * u.y();
        const double dn = dir.x() * n.x() + dir.y() * n.y();
        if (std::fabs(du) > 0.999) {
            sh.axial = true;
            sh.dirSign = du > 0.0 ? 1.0 : -1.0;
        } else if (std::fabs(dn) > 0.999 && std::fabs(sh.s) > 1e-6 && (dn > 0.0) != (sh.s > 0.0)) {
            sh.axial = false;                           // 바깥에서 축 쪽으로
            sh.dirSign = dn > 0.0 ? 1.0 : -1.0;
        } else {
            continue;                                   // 비스듬한 구멍은 아직 지원하지 않음
        }
        // 이 부품의 구멍인가: 입구에서 조금 들어간 점이 단면 재료 안 (축 반대쪽 반단면도 |s| 로)
        const double probe = std::max(0.05, std::min(0.5, sh.thickness * 0.25));
        const double px = sh.x + (sh.axial ? sh.dirSign * probe : 0.0);
        const double ps = sh.s + (sh.axial ? 0.0 : sh.dirSign * probe);
        // 축 위(중심 구멍)는 단면의 축 변 위라 contains 가 흔들림 → 축에서 아주 조금 띄운 점으로 판정
        const QPointF q(px, std::max(std::fabs(ps), 0.01));
        if (!secBox.adjusted(-1e-6, -1e-6, 1e-6, 1e-6).contains(q) || !sec.contains(q))
            continue;
        out << sh;
    }
    return out;
}

namespace {
// 두 경로가 (작은 오차를 빼고) 같은 모양인가
bool samePath(const QPainterPath &a, const QPainterPath &b)
{
    const double tiny = 1e-3;
    const QRectF d1 = a.subtracted(b).boundingRect();
    const QRectF d2 = b.subtracted(a).boundingRect();
    return (d1.width() < tiny || d1.height() < tiny) && (d2.width() < tiny || d2.height() < tiny);
}
} // namespace

void ProjectionView::updatePartHatch(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid,
                                     const QPainterPath &holeArea)
{
    if (!ctx || !scene || !ctx->entityFactory() || !scene->symbolRegistry()->hasSymbol(partUuid))
        return;
    const SymbolInfo info = scene->symbolRegistry()->symbolInfo(partUuid);
    if (info.type != QLatin1String("MechRevolve") ||
        info.props.value(QStringLiteral("Source")).toString() == QLatin1String("area"))
        return;                                         // 해치 방식만 (영역 방식은 해칭이 없음)
    QList<HatchItem *> hatches;
    const QList<QGraphicsItem *> items = scene->items();
    for (int i = 0; i < items.size(); ++i) {
        HatchItem *h = dynamic_cast<HatchItem *>(items.at(i));
        if (h && h->data(MECH_DATA_PART_ID).toString() == partUuid)
            hatches << h;
    }
    for (int i = 0; i < hatches.size(); ++i) {
        HatchItem *old = hatches.at(i);
        const QPainterPath curPath = old->sceneTransform().map(old->geometryPath());
        // 단면 = 해칭이 덮는 선 영역 (이미 구멍을 뺀 해칭이어도 선 영역은 꽉 찬 모양)
        QPainterPath newPath = RevolveProfile::hatchLineRegion(scene, curPath);
        if (!holeArea.isEmpty())
            newPath = newPath.subtracted(holeArea);
        newPath.setFillRule(Qt::OddEvenFill);
        if (newPath.isEmpty() || samePath(newPath, curPath))
            continue;
        // 같은 무늬·축척·각도·레이어로 새 해칭, 부품 태그 유지 → 옛 해칭 삭제 (실행 취소 한 단계 안)
        const QMap<QString, QVariant> hp = old->getProperties();
        const QString layer = old->layerName();
        QGraphicsItem *hi = ctx->entityFactory()->createHatch(newPath, hp.value(QStringLiteral("Pattern")).toString(),
                                                              hp.value(QStringLiteral("Scale"), 1.0).toDouble(),
                                                              hp.value(QStringLiteral("Angle"), 0.0).toDouble(),
                                                              AbstractTool::layerPenFor(ctx, layer));
        if (!hi)
            continue;
        if (CadItem *cad = dynamic_cast<CadItem *>(hi))
            cad->setLayerName(layer);
        hi->setData(MECH_DATA_PART_ID, partUuid);
        hi->setParentItem(scene->getLayerRoot(layer));
        if (hi->scene() == scene)
            scene->removeItem(hi);
        if (ctx->undoStack()) {
            ctx->undoStack()->push(new AddEntityCommand(scene, hi));
            ctx->undoStack()->push(new RemoveEntityCommand(scene, old));
        } else {
            scene->addItem(hi);
            scene->removeItem(old);
        }
    }
}

int ProjectionView::syncHoleLinks(IAppContext *ctx, MyGraphicsScene *scene)
{
    if (!ctx || !scene)
        return 0;
    int n = 0;
    const QMap<QString, SymbolInfo> all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        const bool plate = it.value().type == QLatin1String("MechExtrude") &&
                           it.value().props.value(QStringLiteral("Standalone")).toBool();
        if (!holeHost(it.value().type) && !plate)
            continue;
        // 투상도가 있거나, 정면에 구멍 단면 도구로 그린 구멍이 있는 회전 부품 (해칭에서 구멍 자리를 빼야 함)
        //   판(단독 돌출)은 투상도가 있을 때만 (구멍은 3D 가 매번 다시 찾음)
        bool need = !viewsOf(it.value()).isEmpty();
        if (!need && !plate) {
            QList<RevolveLoop> loops;
            QPointF a0;
            double angleDeg = 0.0;
            QString name;
            if (MechModel3D::revolveSection(scene, it.key(), loops, a0, angleDeg, name))
                need = !sectionHolesOf(scene, it.key(), loops, a0, angleDeg).isEmpty();
        }
        if (need)
            n += regeneratePart(ctx, scene, it.key());
    }
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

// ── 회전체 보조 투상도 도우미 (부품 로컬 (y, z) 평면) ──
inline double dot2(const QPointF &a, const QPointF &b) { return a.x() * b.x() + a.y() * b.y(); }
inline double cross2(const QPointF &a, const QPointF &b) { return a.x() * b.y() - a.y() * b.x(); }

// 직선 o + τ·d 가 고리들(짝홀 규칙) 안에 있는 τ 구간들 [a, b]
QList<QPair<double, double>> rayIntervals(const QList<QPolygonF> &rings, const QPointF &o, const QPointF &d)
{
    QList<double> ts;
    for (int r = 0; r < rings.size(); ++r) {
        const QPolygonF &p = rings.at(r);
        for (int k = 0; k < p.size(); ++k) {
            const QPointF a = p.at(k), b = p.at((k + 1) % p.size());
            const QPointF ab = b - a;
            const double den = cross2(d, ab);
            if (std::fabs(den) < 1e-12)
                continue;                               // 나란한 변
            const QPointF ao = a - o;
            const double t = cross2(ao, ab) / den;
            const double lam = cross2(ao, d) / den;
            if (lam >= 0.0 && lam < 1.0)
                ts << t;
        }
    }
    std::sort(ts.begin(), ts.end());
    QList<QPair<double, double>> out;
    for (int i = 0; i + 1 < ts.size(); i += 2)
        if (ts.at(i + 1) - ts.at(i) > 1e-9)
            out << qMakePair(ts.at(i), ts.at(i + 1));
    return out;
}

// 점 q 에서 보는 쪽(+d)으로 나아가는 시선이 고리 재료를 지나는가 (q 바로 앞은 제외)
bool rayBlocked(const QList<QPolygonF> &rings, const QPointF &q, const QPointF &d)
{
    const QList<QPair<double, double>> iv = rayIntervals(rings, q, d);
    for (int i = 0; i < iv.size(); ++i)
        if (iv.at(i).second > std::max(iv.at(i).first, 1e-6) + 1e-6)
            return true;
    return false;
}

// 보는 방향 φ → dn(보는 쪽), e(뷰 위쪽) — 로컬 (y, z)
void revFaceAxes(int kind, QPointF &dn, QPointF &e)
{
    const double ph = qDegreesToRadians(ProjectionView::revFaceAngle(kind));
    dn = QPointF(std::cos(ph), std::sin(ph));
    e = QPointF(std::sin(ph), -std::cos(ph));
}

// 붙은 돌출(파내기 제외) 해석 목록
QList<ExtrudeInfo> solidExtrudes(MyGraphicsScene *scene, const QString &hostUuid, QStringList *ids = nullptr)
{
    QList<ExtrudeInfo> out;
    const QList<QString> all = ExtrudeFeature::attachedTo(scene, hostUuid);
    for (int i = 0; i < all.size(); ++i) {
        ExtrudeInfo ei;
        if (!ExtrudeFeature::resolve(scene, all.at(i), ei) || ei.cut || ei.plateBoss || ei.plateFront || ei.standalone)
            continue;
        out << ei;
        if (ids)
            *ids << all.at(i);
    }
    return out;
}

// 로컬 점 (x, q) 가 보는 쪽(dn)에서 가려지는가: 회전체 바깥 외형(반지름 R(x)) 또는 다른 돌출
bool auxPointHidden(const QList<RevolveLoop> &loops, const QList<ExtrudeInfo> &exts, const QString &selfUuid,
                    double x, const QPointF &q, const QPointF &dn, const QPointF &e)
{
    const double R = hostR(loops, x);
    const double sv = dot2(q, e), tq = dot2(q, dn);
    if (std::fabs(sv) < R - 1e-6 && tq < std::sqrt(std::max(0.0, R * R - sv * sv)) - 1e-6)
        return true;                                    // 회전체 앞면이 더 앞
    for (int i = 0; i < exts.size(); ++i) {
        const ExtrudeInfo &o = exts.at(i);
        if (o.uuid == selfUuid || x <= o.x0 + 1e-6 || x >= o.x1 - 1e-6)
            continue;
        if (rayBlocked(o.rings, q, dn))
            return true;
    }
    return false;
}

// 회전체 보조 투상도의 돌출 외형 (vis/hid) — 각진 꼭짓점의 축 방향 선 + 양 끝면 세로선
void auxExtrudeLines(MyGraphicsScene *scene, const QString &hostUuid, const QString &selfUuid, const ExtrudeInfo &ei,
                     int kind, double param, const QList<RevolveLoop> &loops, const QPointF &a0, double angleDeg,
                     QList<QLineF> &vis, QList<QLineF> &hid)
{
    QPointF dn, e;
    revFaceAxes(kind, dn, e);
    const double ang = qDegreesToRadians(angleDeg);
    const QPointF u(std::cos(ang), std::sin(ang)), n(-std::sin(ang), std::cos(ang));
    const QPointF base = a0 + n * param;
    const QList<ExtrudeInfo> exts = solidExtrudes(scene, hostUuid);
    QList<double> xBreaks;                              // 가림이 바뀌는 x (회전체 꼭짓점·다른 돌출 끝)
    for (int i = 0; i < loops.size(); ++i)
        for (int k = 0; k < loops.at(i).pts.size(); ++k)
            xBreaks << loops.at(i).pts.at(k).x();
    for (int i = 0; i < exts.size(); ++i)
        xBreaks << exts.at(i).x0 << exts.at(i).x1;
    std::sort(xBreaks.begin(), xBreaks.end());

    if (ei.cut) {
        // 파내기(홈): 재료(회전체) 속이면 숨은선 — 평면도와 같은 규칙
        for (int r = 0; r < ei.rings.size(); ++r)
            for (int k = 0; k < ei.rings.at(r).size(); ++k) {
                const QPointF v = ei.rings.at(r).at(k);
                const double sv = dot2(v, e);
                if (std::fabs(sv) < hostR(loops, (ei.x0 + ei.x1) * 0.5))
                    hid << QLineF(base + u * ei.x0 + n * sv, base + u * ei.x1 + n * sv);
            }
        return;
    }

    // ① 꼭짓점 선: 같은 높이(s)면 가장 앞(t 큰) 꼭짓점 하나
    double smin = 1e300, smax = -1e300;
    for (int r = 0; r < ei.rings.size(); ++r)
        for (int k = 0; k < ei.rings.at(r).size(); ++k) {
            const double sv = dot2(ei.rings.at(r).at(k), e);
            smin = std::min(smin, sv);
            smax = std::max(smax, sv);
        }
    QList<QPointF> cand;                                // (s, t)
    for (int r = 0; r < ei.rings.size(); ++r) {
        const QPolygonF &p = ei.rings.at(r);
        for (int k = 0; k < p.size(); ++k) {
            const double sv = dot2(p.at(k), e), tv = dot2(p.at(k), dn);
            if (!sharpYZ(p, k) && std::fabs(sv - smin) > 1e-6 && std::fabs(sv - smax) > 1e-6)
                continue;
            bool merged = false;
            for (int c = 0; c < cand.size() && !merged; ++c)
                if (std::fabs(cand.at(c).x() - sv) < 1e-4) {
                    if (tv > cand.at(c).y())
                        cand[c].setY(tv);
                    merged = true;
                }
            if (!merged)
                cand << QPointF(sv, tv);
        }
    }
    for (int c = 0; c < cand.size(); ++c) {
        const double sv = cand.at(c).x(), tv = cand.at(c).y();
        const QPointF q = e * sv + dn * tv;
        const bool self = rayBlocked(ei.rings, q, dn);  // 돌출 자기 몸에 가려짐
        QList<double> cuts;
        cuts << ei.x0;
        for (int t = 0; t < xBreaks.size(); ++t)
            if (xBreaks.at(t) > ei.x0 + 1e-6 && xBreaks.at(t) < ei.x1 - 1e-6)
                cuts << xBreaks.at(t);
        cuts << ei.x1;
        for (int t = 0; t + 1 < cuts.size(); ++t) {
            const double xa = cuts.at(t), xb = cuts.at(t + 1);
            if (xb - xa < 1e-6)
                continue;
            const bool hidden = self || auxPointHidden(loops, exts, ei.uuid, (xa + xb) * 0.5, q, dn, e);
            (hidden ? hid : vis) << QLineF(base + u * xa + n * sv, base + u * xb + n * sv);
        }
    }
    // ② 양 끝면 세로선 (x0, x1): s 구간을 회전체 외형 ±R·꼭짓점 높이로 잘라 조각별 판정
    const double xs[2] = {ei.x0, ei.x1};
    for (int j = 0; j < 2; ++j) {
        const double R = hostR(loops, xs[j]);
        QList<double> cs;
        cs << smin;
        for (int c = 0; c < cand.size(); ++c)
            cs << cand.at(c).x();
        if (-R > smin && -R < smax)
            cs << -R;
        if (R > smin && R < smax)
            cs << R;
        cs << smax;
        std::sort(cs.begin(), cs.end());
        for (int t = 0; t + 1 < cs.size(); ++t) {
            const double sa = cs.at(t), sb = cs.at(t + 1);
            if (sb - sa < 1e-6)
                continue;
            const double sm = (sa + sb) * 0.5;
            // 그 높이의 돌출 앞면 깊이 (가장 앞 구간 끝)
            const QList<QPair<double, double>> iv = rayIntervals(ei.rings, e * sm, dn);
            if (iv.isEmpty())
                continue;                               // 오목한 곳: 재료 없음
            double tf = -1e300;
            for (int i = 0; i < iv.size(); ++i)
                tf = std::max(tf, iv.at(i).second);
            // 끝면 위 점은 그 x 바로 안쪽에서 판정 (끝면과 같은 x 의 회전체 턱은 경계)
            const double xin = (j == 0) ? xs[j] + 1e-4 : xs[j] - 1e-4;
            const bool hidden = auxPointHidden(loops, exts, ei.uuid, xin, e * sm + dn * tf, dn, e);
            (hidden ? hid : vis) << QLineF(base + u * xs[j] + n * sa, base + u * xs[j] + n * sb);
        }
    }
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

        if (isRevFaceKind(kind)) {
            auxExtrudeLines(scene, hostUuid, ids.at(e), ei, kind, param, loops, a0, angleDeg, vis, hid);
        } else if (kind == Left || kind == Right) {
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
                    const bool insideHost = std::hypot(m.x(), m.y()) <= rCover + 1e-3;
                    if (ei.cut) {                           // 파내기(홈): 재료 속이면 숨은선, 밖이면 없음
                        if (insideHost)
                            hid << QLineF(c + u * (left ? a.y() : -a.y()) + n * a.x(), c + u * (left ? b.y() : -b.y()) + n * b.x());
                        continue;
                    }
                    if (insideHost)
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
                    if (ei.cut && !covered)
                        continue;                       // 파내기: 재료 밖은 없음
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
                    if (ei.cut && !covered)
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
                                     const QString &viewId, const QPointF &a0, double angleDeg,
                                     const QPainterPath &holeArea)
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

    // 해칭 방식이면 원래 해칭과 같은 무늬로 (반대쪽 구멍 단면 자리는 뺀다)
    if (!holeArea.isEmpty()) {
        hatchPath = hatchPath.subtracted(holeArea);
        hatchPath.setFillRule(Qt::OddEvenFill);
    }
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

// ─────────────────────────────────────────────────────────────────────────────
// 투상도 숨기기 / 보이기
// ─────────────────────────────────────────────────────────────────────────────
namespace {
// 뷰 기록 "concealed" 바꾸기 + 구멍 보이기 상태 맞추기 (실행 취소해도 구멍 상태가 기록을 따라가게 한 명령으로)
class ConcealViewCommand : public QUndoCommand {
public:
    ConcealViewCommand(MyGraphicsScene *scene, const QString &uuid, const SymbolInfo &oldInfo,
                       const SymbolInfo &newInfo)
        : m_scene(scene), m_uuid(uuid), m_old(oldInfo), m_new(newInfo)
    {
        setText(QObject::tr("투상도 숨기기·보이기"));
    }
    void redo() override { apply(m_new); }
    void undo() override { apply(m_old); }

private:
    void apply(const SymbolInfo &info)
    {
        if (!m_scene)
            return;
        m_scene->symbolRegistry()->registerSymbol(m_uuid, info);
        ProjectionView::applyConcealedHoles(m_scene);
    }
    MyGraphicsScene *m_scene;
    QString m_uuid;
    SymbolInfo m_old;
    SymbolInfo m_new;
};
} // namespace

bool ProjectionView::viewOfItem(MyGraphicsScene *scene, const QGraphicsItem *item, QString &partUuid,
                                QString &viewId)
{
    if (!scene || !item)
        return false;
    const QString vid = item->data(MECH_VIEW_ID).toString();
    if (vid.isEmpty())
        return false;
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        const QJsonArray views = viewsOf(it.value());
        for (int i = 0; i < views.size(); ++i) {
            const QJsonObject v = views.at(i).toObject();
            if (v.value(QStringLiteral("id")).toString() != vid)
                continue;
            if (v.value(QStringLiteral("kind")).toInt() == Front)
                return false;                           // 정면 구멍 단면은 숨기지 않음
            partUuid = it.key();
            viewId = vid;
            return true;
        }
    }
    return false;
}

bool ProjectionView::setViewConcealed(IAppContext *ctx, MyGraphicsScene *scene, const QString &partUuid,
                                      const QString &viewId, bool on)
{
    if (!ctx || !scene || !scene->symbolRegistry()->hasSymbol(partUuid))
        return false;
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    const SymbolInfo oldInfo = reg->symbolInfo(partUuid);
    QJsonArray views = viewsOf(oldInfo);
    bool found = false;
    for (int i = 0; i < views.size(); ++i) {
        QJsonObject v = views.at(i).toObject();
        if (v.value(QStringLiteral("id")).toString() != viewId || v.value(QStringLiteral("kind")).toInt() == Front)
            continue;
        if (v.value(QStringLiteral("concealed")).toBool() == on)
            return false;                               // 이미 그 상태
        if (on)
            v.insert(QStringLiteral("concealed"), true);
        else
            v.remove(QStringLiteral("concealed"));
        views.replace(i, v);
        found = true;
        break;
    }
    if (!found)
        return false;
    SymbolInfo info = oldInfo;
    info.props[QStringLiteral("Views")] = QString::fromUtf8(QJsonDocument(views).toJson(QJsonDocument::Compact));
    ConcealViewCommand *cmd = new ConcealViewCommand(scene, partUuid, oldInfo, info);
    if (ctx->undoStack())
        ctx->undoStack()->push(cmd);
    else {
        cmd->redo();
        delete cmd;
    }
    // 숨기기 = 그 뷰 선을 지움 (기록은 숨김이라 다시 그리지 않음) / 보이기 = 다시 그림
    regeneratePart(ctx, scene, partUuid);
    return true;
}

QList<ProjectionView::ConcealedView> ProjectionView::concealedViews(MyGraphicsScene *scene)
{
    QList<ConcealedView> out;
    if (!scene)
        return out;
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        const QJsonArray views = viewsOf(it.value());
        for (int i = 0; i < views.size(); ++i) {
            const QJsonObject v = views.at(i).toObject();
            if (!v.value(QStringLiteral("concealed")).toBool())
                continue;
            ConcealedView cv;
            cv.partUuid = it.key();
            cv.viewId = v.value(QStringLiteral("id")).toString();
            cv.label = QStringLiteral("%1 — %2").arg(it.value().name, kindName(v.value(QStringLiteral("kind")).toInt()));
            out << cv;
        }
    }
    return out;
}

namespace {
// 숨긴 투상도 때문에 숨기는 표시 (원래 사용자가 숨긴 것은 건드리지 않도록 MECH_CONCEALED_HOLE 로 구분)
void setConcealed(QGraphicsItem *gi, bool hide)
{
    const bool was = gi->data(MECH_CONCEALED_HOLE).toBool();
    if (hide && !was) {
        gi->setData(MECH_CONCEALED_HOLE, true);
        gi->setVisible(false);
    } else if (!hide && was) {
        gi->setData(MECH_CONCEALED_HOLE, QVariant());
        gi->setVisible(true);
    }
}
} // namespace

void ProjectionView::applyConcealedHoles(MyGraphicsScene *scene)
{
    if (!scene)
        return;
    // 숨긴 측면도들 (회전 부품만 — 구멍 연결이 측면도 기준)
    QList<SideViewInfo> hiddenViews;
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        const QJsonArray views = viewsOf(it.value());
        bool any = false;
        for (int i = 0; i < views.size() && !any; ++i)
            any = views.at(i).toObject().value(QStringLiteral("concealed")).toBool();
        if (!any)
            continue;
        const QList<SideViewInfo> svs = sideViews(scene, it.key());
        for (int k = 0; k < svs.size(); ++k)
            if (svs.at(k).concealed)
                hiddenViews << svs.at(k);
    }
    // 숨긴 회전체 보조 투상도(받침대 면)에 놓은 구멍 — 그 뷰를 숨기면 함께 숨김
    QSet<const QGraphicsItem *> auxHidden;
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        const QJsonArray views = viewsOf(it.value());
        QSet<QString> hiddenAux;
        for (int i = 0; i < views.size(); ++i) {
            const QJsonObject v = views.at(i).toObject();
            if (v.value(QStringLiteral("concealed")).toBool() && isRevFaceKind(v.value(QStringLiteral("kind")).toInt()))
                hiddenAux.insert(v.value(QStringLiteral("id")).toString());
        }
        if (hiddenAux.isEmpty())
            continue;
        const QList<AuxHole> ahs = auxHolesOf(scene, it.key());
        for (int h = 0; h < ahs.size(); ++h)
            if (ahs.at(h).item && hiddenAux.contains(ahs.at(h).viewId))
                auxHidden.insert(ahs.at(h).item);
    }
    // 숨긴 판 투상도(측면도·평면도·보조 투상도)에 놓은 판 구멍 — 함께 숨김 (정면 구멍은 정면을 숨길 수 없어 해당 없음)
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        if (!PlateView::isPlate(scene, it.key()))
            continue;
        const QJsonArray views = viewsOf(it.value());
        QSet<QString> hiddenIds;
        for (int i = 0; i < views.size(); ++i) {
            const QJsonObject v = views.at(i).toObject();
            if (v.value(QStringLiteral("concealed")).toBool() && v.value(QStringLiteral("kind")).toInt() != Front)
                hiddenIds.insert(v.value(QStringLiteral("id")).toString());
        }
        if (hiddenIds.isEmpty())
            continue;
        const QList<PlateHole> phs = PlateView::holes(scene, it.key());
        for (int h = 0; h < phs.size(); ++h)
            if (phs.at(h).item && hiddenIds.contains(phs.at(h).viewId))
                auxHidden.insert(phs.at(h).item);
    }
    const QList<QGraphicsItem *> items = scene->items();
    QSet<QString> hiddenGids;
    for (int i = 0; i < items.size(); ++i) {
        HoleItem *hi = dynamic_cast<HoleItem *>(items.at(i));
        if (!hi)
            continue;
        const MyHole &mh = hi->holeData();
        const QPointF p = hi->sceneTransform().map(QPointF(mh.position.x, mh.position.y));
        bool inHidden = auxHidden.contains(hi);
        for (int k = 0; k < hiddenViews.size() && !inHidden; ++k) {
            const QPointF d = p - hiddenViews.at(k).center;
            inHidden = std::hypot(d.x(), d.y()) <= hiddenViews.at(k).rMax + 1e-6;
        }
        const QString gid = hi->data(HOLE_DATA_GROUP_ID).toString();
        if (inHidden && !gid.isEmpty())
            hiddenGids.insert(gid);
        setConcealed(hi, inHidden);
    }
    // 구멍과 함께 만들어진 중심선(같은 HOLE_DATA_GROUP_ID 의 독립 선)도 같이 숨김·보이기
    for (int i = 0; i < items.size(); ++i) {
        QGraphicsItem *gi = items.at(i);
        if (dynamic_cast<HoleItem *>(gi))
            continue;
        const QString gid = gi->data(HOLE_DATA_GROUP_ID).toString();
        if (gid.isEmpty())
            continue;
        setConcealed(gi, hiddenGids.contains(gid));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 회전체 보조 투상도 (붙은 돌출의 면을 정면으로 봄) — 면 고르기·가림·구멍
// ─────────────────────────────────────────────────────────────────────────────
bool ProjectionView::revFaceAt(MyGraphicsScene *scene, const QString &partUuid, const QPointF &pos, double &phiDeg,
                               QLineF *edge, double maxDist)
{
    if (!scene)
        return false;
    const QList<ExtrudeInfo> exts = solidExtrudes(scene, partUuid);
    if (exts.isEmpty())
        return false;
    const QList<SideViewInfo> svs = sideViews(scene, partUuid);
    double best = 1e300;
    for (int v = 0; v < svs.size(); ++v) {
        const SideViewInfo &sv = svs.at(v);
        if (sv.concealed)
            continue;
        const bool left = (sv.kind == Left);
        for (int i = 0; i < exts.size(); ++i) {
            const QList<QPolygonF> &rings = exts.at(i).rings;
            for (int r = 0; r < rings.size(); ++r) {
                const QPolygonF &p = rings.at(r);
                double area = 0.0;
                for (int k = 0; k < p.size(); ++k)
                    area += cross2(p.at(k), p.at((k + 1) % p.size()));
                for (int k = 0; k < p.size(); ++k) {
                    const QPointF a = p.at(k), b = p.at((k + 1) % p.size());
                    // 측면도 위 점: 중심 + u·(좌 = z, 우 = −z) + n·y
                    const QPointF A = sv.center + sv.u * (left ? a.y() : -a.y()) + sv.n * a.x();
                    const QPointF B = sv.center + sv.u * (left ? b.y() : -b.y()) + sv.n * b.x();
                    const QPointF ab = B - A;
                    const double l2 = dot2(ab, ab);
                    if (l2 < 1e-12)
                        continue;
                    const double t = std::clamp(dot2(pos - A, ab) / l2, 0.0, 1.0);
                    const double dist = std::hypot(pos.x() - (A + ab * t).x(), pos.y() - (A + ab * t).y());
                    if (dist >= best || (maxDist > 0.0 && dist > maxDist))
                        continue;
                    // 바깥 법선 (y, z): 반시계 고리면 변의 오른쪽, 시계(구멍) 고리면 왼쪽
                    const QPointF dd = b - a;
                    QPointF nrm(dd.y(), -dd.x());
                    if (area < 0.0)
                        nrm = -nrm;
                    best = dist;
                    phiDeg = qRadiansToDegrees(std::atan2(nrm.y(), nrm.x()));
                    if (edge)
                        *edge = QLineF(A, B);
                }
            }
        }
    }
    return best < 1e300;
}

QList<ProjectionView::AuxHole> ProjectionView::auxHolesOf(MyGraphicsScene *scene, const QString &hostUuid)
{
    QList<AuxHole> out;
    if (!scene || !scene->symbolRegistry()->hasSymbol(hostUuid))
        return out;
    const SymbolInfo info = scene->symbolRegistry()->symbolInfo(hostUuid);
    if (info.type != QLatin1String("MechRevolve"))
        return out;
    const QJsonArray views = viewsOf(info);
    bool any = false;
    for (int i = 0; i < views.size() && !any; ++i)
        any = isRevFaceKind(views.at(i).toObject().value(QStringLiteral("kind")).toInt());
    if (!any)
        return out;
    QStringList extIds;
    const QList<ExtrudeInfo> exts = solidExtrudes(scene, hostUuid, &extIds);
    if (exts.isEmpty())
        return out;
    QList<RevolveLoop> loops;
    QPointF a0, ref;
    double angleDeg = 0.0;
    QString name;
    if (!MechModel3D::revolveSection(scene, hostUuid, loops, a0, angleDeg, name, &ref))
        return out;
    const double ang = qDegreesToRadians(angleDeg);
    const QPointF u(std::cos(ang), std::sin(ang)), n(-std::sin(ang), std::cos(ang));
    // 뷰의 높이 범위(회전체 반지름·돌출 단면)와 축 방향 범위
    double extent = 0.0, xMin = 1e300, xMax = -1e300;
    for (int i = 0; i < loops.size(); ++i)
        for (int k = 0; k < loops.at(i).pts.size(); ++k) {
            extent = std::max(extent, loops.at(i).pts.at(k).y());
            xMin = std::min(xMin, loops.at(i).pts.at(k).x());
            xMax = std::max(xMax, loops.at(i).pts.at(k).x());
        }
    for (int i = 0; i < exts.size(); ++i) {
        xMin = std::min(xMin, exts.at(i).x0);
        xMax = std::max(xMax, exts.at(i).x1);
        for (int r = 0; r < exts.at(i).rings.size(); ++r)
            for (int k = 0; k < exts.at(i).rings.at(r).size(); ++k) {
                const QPointF v = exts.at(i).rings.at(r).at(k);
                extent = std::max(extent, std::hypot(v.x(), v.y()));
            }
    }
    struct AuxView {
        QString id;
        int kind;
        QPointF base;
    };
    QList<AuxView> avs;
    for (int i = 0; i < views.size(); ++i) {
        const QJsonObject v = views.at(i).toObject();
        const int kind = v.value(QStringLiteral("kind")).toInt();
        if (!isRevFaceKind(kind))
            continue;
        double param = v.value(QStringLiteral("param")).toDouble();
        if (v.contains(QStringLiteral("ax"))) {
            const QPointF anc(v.value(QStringLiteral("ax")).toDouble() + ref.x() - v.value(QStringLiteral("rx")).toDouble(),
                              v.value(QStringLiteral("ay")).toDouble() + ref.y() - v.value(QStringLiteral("ry")).toDouble());
            param = paramFor(kind, a0, angleDeg, anc);
        }
        avs << AuxView{v.value(QStringLiteral("id")).toString(), kind, a0 + n * param};
    }
    const QList<QGraphicsItem *> items = scene->items();
    for (int i = 0; i < items.size(); ++i) {
        const HoleItem *hi = dynamic_cast<const HoleItem *>(items.at(i));
        if (!hi || (!hi->isVisible() && !hi->data(MECH_CONCEALED_HOLE).toBool()))
            continue;
        const MyHole &mh = hi->holeData();
        const QPointF p = hi->sceneTransform().map(QPointF(mh.position.x, mh.position.y));
        const double x = dot2(p - a0, u);
        if (x < xMin - 1e-6 || x > xMax + 1e-6)
            continue;
        for (int v = 0; v < avs.size(); ++v) {
            const double sv = dot2(p - avs.at(v).base, n);
            if (std::fabs(sv) > extent + 1e-6)
                continue;
            QPointF dn, e;
            revFaceAxes(avs.at(v).kind, dn, e);
            // 보는 쪽에서 가장 앞에 있는 돌출 면 (그 x 를 덮는 돌출만)
            int bestE = -1;
            double bestT = -1e300, bestLen = 0.0;
            for (int k = 0; k < exts.size(); ++k) {
                if (x <= exts.at(k).x0 + 1e-6 || x >= exts.at(k).x1 - 1e-6)
                    continue;
                const QList<QPair<double, double>> iv = rayIntervals(exts.at(k).rings, e * sv, dn);
                for (int j = 0; j < iv.size(); ++j)
                    if (iv.at(j).second > bestT) {
                        bestT = iv.at(j).second;
                        bestLen = iv.at(j).second - iv.at(j).first;
                        bestE = k;
                    }
            }
            if (bestE < 0)
                break;                                  // 이 뷰 안이지만 뚫을 돌출 없음
            // 회전체가 그 면보다 앞이면(가려진 자리) 받침대 구멍이 아님
            const double R = hostR(loops, x);
            if (std::fabs(sv) < R - 1e-6 && std::sqrt(std::max(0.0, R * R - sv * sv)) > bestT + 1e-6)
                break;
            AuxHole ah;
            ah.hole = mh;
            ah.groupId = hi->data(HOLE_DATA_GROUP_ID).toString();
            ah.viewId = avs.at(v).id;
            ah.extrudeUuid = extIds.at(bestE);
            ah.x = x;
            ah.entry = e * sv + dn * bestT;
            ah.dir = -dn;
            ah.thickness = bestLen;
            ah.item = hi;
            out << ah;
            break;
        }
    }
    return out;
}

void ProjectionView::occludeByExtrudes(MyGraphicsScene *scene, const QString &hostUuid, int kind, double param,
                                       const QList<RevolveLoop> &loops, const QPointF &a0, double angleDeg,
                                       ProjDrawing &d)
{
    const QList<ExtrudeInfo> exts = solidExtrudes(scene, hostUuid);
    if (exts.isEmpty() || !isRevFaceKind(kind))
        return;
    QPointF dn, e;
    revFaceAxes(kind, dn, e);
    const double ang = qDegreesToRadians(angleDeg);
    const QPointF u(std::cos(ang), std::sin(ang)), n(-std::sin(ang), std::cos(ang));
    const QPointF base = a0 + n * param;
    // 돌출의 x 끝·높이 끝 (선을 자를 곳)
    QList<double> xCuts, sCuts;
    for (int i = 0; i < exts.size(); ++i) {
        xCuts << exts.at(i).x0 << exts.at(i).x1;
        for (int r = 0; r < exts.at(i).rings.size(); ++r)
            for (int k = 0; k < exts.at(i).rings.at(r).size(); ++k)
                sCuts << dot2(exts.at(i).rings.at(r).at(k), e);
    }
    QList<QLineF> keep, moved;
    for (int i = 0; i < d.visible.size(); ++i) {
        const QLineF ln = d.visible.at(i);
        const double xa = dot2(ln.p1() - a0, u), xb = dot2(ln.p2() - a0, u);
        const double sa = dot2(ln.p1() - base, n), sb = dot2(ln.p2() - base, n);
        const bool horiz = std::fabs(sa - sb) < 1e-6;
        const double rho = std::max(std::fabs(sa), std::fabs(sb));   // 세로선 = 그 원(반지름 rho)의 모서리
        QList<double> ts;                               // 선 위 매개변수 0..1 자를 곳
        ts << 0.0 << 1.0;
        const QList<double> &cuts = horiz ? xCuts : sCuts;
        const double c0 = horiz ? xa : sa, c1 = horiz ? xb : sb;
        if (std::fabs(c1 - c0) > 1e-9)
            for (int k = 0; k < cuts.size(); ++k) {
                const double t = (cuts.at(k) - c0) / (c1 - c0);
                if (t > 1e-6 && t < 1.0 - 1e-6)
                    ts << t;
            }
        std::sort(ts.begin(), ts.end());
        for (int k = 0; k + 1 < ts.size(); ++k) {
            const double ta = ts.at(k), tb = ts.at(k + 1);
            if (tb - ta < 1e-9)
                continue;
            const double tm = (ta + tb) * 0.5;
            const double xm = xa + (xb - xa) * tm, sm = sa + (sb - sa) * tm;
            const double tq = horiz ? 0.0 : std::sqrt(std::max(0.0, rho * rho - sm * sm));   // 외형선의 깊이
            bool hidden = false;
            for (int j = 0; j < exts.size() && !hidden; ++j) {
                if (xm <= exts.at(j).x0 + 1e-6 || xm >= exts.at(j).x1 - 1e-6)
                    continue;
                hidden = rayBlocked(exts.at(j).rings, e * sm + dn * tq, dn);
            }
            (hidden ? moved : keep) << QLineF(ln.pointAt(ta), ln.pointAt(tb));
        }
    }
    d.visible = keep;
    d.hidden << moved;
    Q_UNUSED(loops);
}

void ProjectionView::drawAuxHoles(IAppContext *ctx, MyGraphicsScene *scene, const QString &hostUuid,
                                  const QString &viewId, int kind, double param, bool showHidden, const QPointF &a0,
                                  double angleDeg)
{
    const QList<AuxHole> holes = auxHolesOf(scene, hostUuid);
    if (holes.isEmpty())
        return;
    // 받침대 속 구멍은 다른 뷰에서 대개 숨은선 — 정면(단면)은 숨은선 설정과 상관없이 그림 (판 옆면 구멍과 같은 관례)
    if (!showHidden && kind != Front)
        return;
    const double ang = qDegreesToRadians(angleDeg);
    const QPointF u(std::cos(ang), std::sin(ang)), n(-std::sin(ang), std::cos(ang));
    const QPointF anchor = anchorFor(kind, a0, angleDeg, param);
    // 로컬 (x, y, z) → 이 뷰의 도면 점, 보는 쪽 방향 D
    QPointF de, ddn;
    if (isRevFaceKind(kind))
        revFaceAxes(kind, ddn, de);
    double Dx = 0.0, Dy = 0.0, Dz = 1.0;               // 정면 = +z 쪽에서 봄
    if (kind == Top) {
        Dx = 0.0; Dy = 1.0; Dz = 0.0;
    } else if (kind == Left) {
        Dx = -1.0; Dy = 0.0; Dz = 0.0;
    } else if (kind == Right) {
        Dx = 1.0; Dy = 0.0; Dz = 0.0;
    } else if (isRevFaceKind(kind)) {
        Dx = 0.0; Dy = ddn.x(); Dz = ddn.y();
    } else if (kind != Front) {
        return;                                         // 판 보조 투상도 등은 해당 없음
    }
    struct Proj {
        int kind;
        QPointF a0, anchor, u, n, de;
        QPointF at(double x, double y, double z) const
        {
            if (kind == Front)
                return a0 + u * x + n * y;
            if (kind == Top)
                return anchor + u * x + n * (-z);
            if (kind == Left)
                return anchor + u * z + n * y;
            if (kind == Right)
                return anchor + u * (-z) + n * y;
            return anchor + u * x + n * (y * de.x() + z * de.y());   // 회전체 보조 투상도
        }
    };
    const Proj pr{kind, a0, anchor, u, n, de};
    const QString hidLayer = hiddenLayer(ctx);
    Layer *cur = ctx->layerManager()->getCurrentLayer();
    const QString visLayer = cur ? cur->name() : QStringLiteral("0");
    const QPen hidPen = AbstractTool::layerPenFor(ctx, hidLayer);
    const QPen visPen = AbstractTool::layerPenFor(ctx, visLayer);
    EntityFactory *ef = ctx->entityFactory();
    for (int h = 0; h < holes.size(); ++h) {
        const AuxHole &ah = holes.at(h);
        if (ah.viewId == viewId)
            continue;                                   // 사용자가 구멍을 놓은 뷰
        HoleFeature tmp;
        tmp.hole = ah.hole;
        const double L = tmp.drillDepth(ah.thickness);
        const double r = ah.hole.diameter * 0.5;
        const double ex = ah.x, ey = ah.entry.x(), ez = ah.entry.y();
        const double ay = ah.dir.x(), az = ah.dir.y();   // 축 방향 (x 성분 0)
        const QPointF P0 = pr.at(ex, ey, ez), P1 = pr.at(ex, ey + ay * L, ez + az * L);
        const QPointF a2 = P1 - P0;
        const double al = std::hypot(a2.x(), a2.y());
        const double DA = Dy * ay + Dz * az;            // 보는 방향 · 구멍 방향
        if (al < 1e-6 * std::max(1.0, L)) {
            // 구멍을 정면으로 봄 → 원: 입구가 보는 쪽이면(또는 관통이면 반대쪽도) 실선, 아니면 숨은선
            const bool open = DA < -0.999 || (ah.hole.throughHole && DA > 0.999);
            if (open)
                addItem(ctx, scene, ef->createCircle(P0, r, visPen), visLayer, viewId);
            else
                addItem(ctx, scene, ef->createCircle(P0, r, hidPen), hidLayer, viewId);
            continue;
        }
        const QPointF w = a2 / al, m(-w.y(), w.x());
        for (int sgn = -1; sgn <= 1; sgn += 2)
            addItem(ctx, scene, ef->createLine(QLineF(P0 + m * (sgn * r), P1 + m * (sgn * r)), hidPen), hidLayer, viewId);
    }
    Q_UNUSED(Dx);
}
