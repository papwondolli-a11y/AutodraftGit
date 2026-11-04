#include "spacefinish.h"

#include "abstracttool.h"
#include "cadcommands.h"
#include "iappcontext.h"
#include "lineitem.h"
#include "multilinedata.h"
#include "mygraphicsscene.h"
#include "myline.h"
#include "planarchdata.h"
#include "tooldefaults.h"

#include <QGraphicsItem>
#include <QUndoStack>
#include <algorithm>
#include <cmath>

int SpaceFinish::s_type = 0;

static const double SPACE_FINISH_EXT = 100.0;

double SpaceFinish::extLength() { return SPACE_FINISH_EXT; }
int    SpaceFinish::type()      { return s_type; }
void   SpaceFinish::setType(int t) { s_type = qBound(0, t, 3); }

void SpaceFinish::load(MyGraphicsScene *scene, const QString &ns)
{
    ToolDefaults d(ns, scene);
    setType(d.getInt("Type", 0));
}

void SpaceFinish::save(MyGraphicsScene *scene, const QString &ns)
{
    ToolDefaults d(ns, scene);
    d.put("Type", s_type);
}

// ── 내부 헬퍼 ───────────────────────────────────────────────────────────────
struct SpaceCrossing {
    LineItem *item;
    double    dist;   // 가상선 시작점에서 교점까지 거리
};

static bool spaceCrossingLess(const SpaceCrossing &a, const SpaceCrossing &b)
{
    return a.dist < b.dist;
}

static double cross2(double ax, double ay, double bx, double by)
{
    return ax * by - ay * bx;
}

// 가상선(p1-p2)과 교차하는 space 선을 p1 에서 가까운 순으로 반환
static QList<SpaceCrossing> collectSpaceCrossings(
    MyGraphicsScene *scene, const QString &groupId,
    const QPointF &p1, const QPointF &p2, double c, double s2)
{
    QList<SpaceCrossing> out;
    if (!scene || groupId.isEmpty())
        return out;

    const double dx  = p2.x() - p1.x();
    const double dy  = p2.y() - p1.y();
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6)
        return out;

    const double tol = 2.0;   // 끝점 허용 오차 (mm)

    const QList<QGraphicsItem *> items = scene->items();
    for (int i = 0; i < items.size(); ++i) {
        LineItem *li = dynamic_cast<LineItem *>(items.at(i));
        if (!li) continue;
        if (li->data(ML_DATA_GROUP_ID).toString() != groupId) continue;
        if (li->data(ML_DATA_ROLE).toString() != "space") continue;

        const MyLine ln   = li->getLine();
        const double ex   = ln.end.x - ln.start.x;
        const double ey   = ln.end.y - ln.start.y;
        const double elen = std::sqrt(ex * ex + ey * ey);
        if (elen < 1e-9) continue;

        // 개구부 진행 방향에 평행한 space 선만 (벽 직교 방향 space 선 제외)
        if (std::abs((ex / elen) * c + (ey / elen) * s2) < 0.5) continue;

        const double denom = cross2(dx, dy, ex, ey);
        if (std::abs(denom) < 1e-9 * len * elen) continue;   // 가상선과 평행

        const double ax = ln.start.x - p1.x();
        const double ay = ln.start.y - p1.y();
        const double t  = cross2(ax, ay, ex, ey) / denom;   // 가상선 위 파라미터
        const double u  = cross2(ax, ay, dx, dy) / denom;   // space 선 위 파라미터

        if (t * len < -tol || t * len > len + tol) continue;
        if (u * elen < -tol || u * elen > elen + tol) continue;

        SpaceCrossing sc;
        sc.item = li;
        sc.dist = t * len;
        out << sc;
    }

    std::sort(out.begin(), out.end(), spaceCrossingLess);

    // 같은 지점에서 만난 중복(분할 조각 등) 제거
    QList<SpaceCrossing> uniq;
    for (int i = 0; i < out.size(); ++i) {
        if (!uniq.isEmpty() && std::abs(out.at(i).dist - uniq.last().dist) < 1.0)
            continue;
        uniq << out.at(i);
    }
    return uniq;
}

// 로컬 X = lx 에서 선분이 지나는 세계 좌표 (선분이 X 방향으로 걸쳐 있지 않으면 false)
static bool wallLinePointAtLocalX(LineItem *li, double ox, double oy,
                                  double c, double s2, double lx, MyVector &out)
{
    const MyLine ln = li->getLine();
    const double d1x = ln.start.x - ox, d1y = ln.start.y - oy;
    const double d2x = ln.end.x   - ox, d2y = ln.end.y   - oy;
    const double l1x = d1x * c + d1y * s2, l1y = d1y * c - d1x * s2;
    const double l2x = d2x * c + d2y * s2, l2y = d2y * c - d2x * s2;

    const double span = l2x - l1x;
    if (std::abs(span) < 1e-9)
        return false;

    const double t  = (lx - l1x) / span;
    const double ly = l1y + t * (l2y - l1y);
    out = MyVector(ox + lx * c - ly * s2, oy + lx * s2 + ly * c);
    return true;
}

// ── planSide ────────────────────────────────────────────────────────────────
SpaceFinish::Plan SpaceFinish::planSide(
    MyGraphicsScene *scene, int type, const QString &wallGroupId,
    const QPointF &p1, const QPointF &p2,
    double ox, double oy, double c, double s2,
    bool rightSide, double W)
{
    Plan plan;
    if (type <= 0 || type > 3)
        return plan;

    // 좌측: P1-P2 가상선, 우측: 개구부 너비(W)만큼 진행 방향(+X)으로 평행이동한 가상선
    QPointF vp1 = p1, vp2 = p2;
    if (rightSide) {
        vp1 += QPointF(c * W, s2 * W);
        vp2 += QPointF(c * W, s2 * W);
    }
    // 더 끊어낸 끝점의 로컬 X: 좌측은 -EXT, 우측은 W+EXT (대칭)
    const double endLx = rightSide ? W + SPACE_FINISH_EXT : -SPACE_FINISH_EXT;

    const QList<SpaceCrossing> hits = collectSpaceCrossings(scene, wallGroupId, vp1, vp2, c, s2);
    if (hits.size() < 2)
        return plan;   // space 선이 없거나 1개뿐 → 조건 무시

    LineItem *first  = hits.at(0).item;
    LineItem *second = hits.at(1).item;

    MyVector a, b;
    if (!wallLinePointAtLocalX(first,  ox, oy, c, s2, endLx, a)) return plan;
    if (!wallLinePointAtLocalX(second, ox, oy, c, s2, endLx, b)) return plan;

    LineItem *base = first;
    switch (type) {
    case 1:   // swtype2
        plan.extItems << first;
        base = first;
        break;
    case 2:   // swtype3
        plan.extItems << second;
        base = second;
        break;
    default:  // swtype4
        plan.extItems << first << second;
        base = first;
        break;
    }

    plan.ext          = SPACE_FINISH_EXT;
    plan.connA        = a;
    plan.connB        = b;
    plan.lyA          = (a.y - oy) * c - (a.x - ox) * s2;
    plan.lyB          = (b.y - oy) * c - (b.x - ox) * s2;
    plan.hasConnector = (std::abs(a.x - b.x) > 1e-4 || std::abs(a.y - b.y) > 1e-4);
    plan.layer        = base->layerName();
    plan.pen          = base->pen();
    return plan;
}

void SpaceFinish::planBoth(MyGraphicsScene *scene, const QString &wallGroupId,
                           const QPointF &p1, const QPointF &p2,
                           double ox, double oy, double c, double s2, double W,
                           Plan &left, Plan &right)
{
    left  = Plan();
    right = Plan();
    if (wallGroupId.isEmpty())
        return;
    left  = planSide(scene, s_type, wallGroupId, p1, p2, ox, oy, c, s2, false, W);
    right = planSide(scene, s_type, wallGroupId, p1, p2, ox, oy, c, s2, true,  W);
}

void SpaceFinish::storeProps(QVariantMap &props, const Plan &left, const Plan &right)
{
    props["SpaceExtL"]       = left.extItems.isEmpty()  ? 0.0 : left.ext;
    props["SpaceExtR"]       = right.extItems.isEmpty() ? 0.0 : right.ext;
    props["SpaceConnLayerL"] = left.hasConnector  ? left.layer  : QString();
    props["SpaceConnLyAL"]   = left.lyA;
    props["SpaceConnLyBL"]   = left.lyB;
    props["SpaceConnLayerR"] = right.hasConnector ? right.layer : QString();
    props["SpaceConnLyAR"]   = right.lyA;
    props["SpaceConnLyBR"]   = right.lyB;
}

void SpaceFinish::addConnectors(MyGraphicsScene *scene,
                                const Plan &left, const Plan &right,
                                const QString &symbolUuid, QUndoStack *stack)
{
    if (!scene || !stack)
        return;

    const Plan *sides[2] = { &left, &right };
    for (int si = 0; si < 2; ++si) {
        if (!sides[si]->hasConnector)
            continue;
        LineItem *conn = new LineItem(sides[si]->connA, sides[si]->connB);
        conn->setLayerName(sides[si]->layer);
        conn->setPen(sides[si]->pen);
        conn->setData(PD_DATA_GROUP_ID,     symbolUuid);
        conn->setData(PD_DATA_CLOSING_LINE, symbolUuid);
        stack->push(new AddEntityCommand(scene, conn));
    }
}

void SpaceFinish::addConnectorsFromProps(MyGraphicsScene *scene, IAppContext *ctx,
                                         double ox, double oy, double c, double s2, double W,
                                         const QVariantMap &props,
                                         const QString &symbolUuid, QUndoStack *stack)
{
    if (!scene || !stack)
        return;

    for (int side = 0; side < 2; ++side) {
        const char   *sfx   = (side == 0) ? "L" : "R";
        const double  ext   = props.value(QString("SpaceExt%1").arg(sfx)).toDouble();
        const QString layer = props.value(QString("SpaceConnLayer%1").arg(sfx)).toString();
        if (ext <= 0.0 || layer.isEmpty())
            continue;

        const double lyA = props.value(QString("SpaceConnLyA%1").arg(sfx)).toDouble();
        const double lyB = props.value(QString("SpaceConnLyB%1").arg(sfx)).toDouble();
        if (std::abs(lyA - lyB) < 1e-4)
            continue;

        const double lx = (side == 0) ? -ext : W + ext;
        const MyVector a(ox + lx * c - lyA * s2, oy + lx * s2 + lyA * c);
        const MyVector b(ox + lx * c - lyB * s2, oy + lx * s2 + lyB * c);

        LineItem *li = new LineItem(a, b);
        li->setLayerName(layer);
        li->setPen(AbstractTool::layerPenFor(ctx, layer));
        li->setData(PD_DATA_GROUP_ID,     symbolUuid);
        li->setData(PD_DATA_CLOSING_LINE, symbolUuid);
        stack->push(new AddEntityCommand(scene, li));
    }
}
