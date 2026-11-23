#include "revolveprofile.h"

#include "advancedboundaryfinder.h"
#include "caditem.h"
#include "hatchitem.h"
#include "holedata.h"
#include "holefeature.h"
#include "holeitem.h"
#include "mechpartdata.h"
#include "mechparttag.h"
#include "mygraphicsscene.h"
#include "scenesymbolregistry.h"

#include <QUndoStack>
#include <QHash>
#include <QPainterPathStroker>
#include <QSet>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTransform>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {

const double kSnap = 1e-6;   // 좌표 정리 단위 (mm)

double snap(double v)
{
    const double r = std::round(v / kSnap) * kSnap;
    return (std::fabs(r) < kSnap) ? 0.0 : r;
}

double signedArea(const QPolygonF &p)
{
    double a = 0.0;
    for (int i = 0; i < p.size(); ++i) {
        const QPointF &u = p.at(i);
        const QPointF &v = p.at((i + 1) % p.size());
        a += u.x() * v.y() - v.x() * u.y();
    }
    return a * 0.5;
}

double absAreaSum(const QPainterPath &path)
{
    double a = 0.0;
    const QList<QPolygonF> polys = path.toFillPolygons();
    for (int i = 0; i < polys.size(); ++i)
        a += std::fabs(signedArea(polys.at(i)));
    return a;
}

// 닫힘점·연속 중복점 제거 + 좌표 정리
QPolygonF cleanLoop(const QPolygonF &in)
{
    QPolygonF out;
    for (int i = 0; i < in.size(); ++i) {
        const QPointF p(snap(in.at(i).x()), std::max(0.0, snap(in.at(i).y())));
        if (!out.isEmpty() && QLineF(out.last(), p).length() < 1e-5)
            continue;
        out << p;
    }
    while (out.size() > 1 && QLineF(out.first(), out.last()).length() < 1e-5)
        out.removeLast();
    return out;
}

// 도면 좌표 → 축 좌표 (x = 축 방향, y = 축에서 떨어진 쪽, 위쪽 +)
QTransform toAxisFrame(const QPointF &a0, const QPointF &a1)
{
    const double ang = std::atan2(a1.y() - a0.y(), a1.x() - a0.x());
    const double c = std::cos(ang), s = std::sin(ang);
    // x' = c(x-ax) + s(y-ay), y' = -s(x-ax) + c(y-ay)
    return QTransform(c, -s, s, c, -(c * a0.x() + s * a0.y()), s * a0.x() - c * a0.y());
}

// 정렬: 구멍 먼저 → 지름 → 시작 위치
bool cylinderLess(const RevolveCylinder &l, const RevolveCylinder &r)
{
    if (l.hole != r.hole)
        return l.hole;
    if (std::fabs(l.diameter - r.diameter) > 1e-6)
        return l.diameter < r.diameter;
    return l.x0 < r.x0;
}

} // namespace

QString RevolveCylinder::keyFor(bool hole, double diameter)
{
    const double d = std::round(diameter * 1000.0) / 1000.0;
    return QStringLiteral("%1:%2").arg(hole ? QStringLiteral("H") : QStringLiteral("S"), QString::number(d, 'g', 10));
}

QString RevolveCylinder::key() const
{
    return keyFor(hole, diameter);
}

void RevolveProfile::normalizeAxis(QPointF &a0, QPointF &a1)
{
    const double dx = a1.x() - a0.x(), dy = a1.y() - a0.y();
    if (dx < -1e-9 || (std::fabs(dx) <= 1e-9 && dy < 0.0))
        std::swap(a0, a1);
}

namespace {
// 축 좌표의 위쪽 단면, 아래쪽 단면(위로 뒤집은 것)
bool splitHalves(const QList<QPainterPath> &hatchPaths, const QPointF &a0, const QPointF &a1, QPainterPath &up,
                 QPainterPath &loMirrored)
{
    if (hatchPaths.isEmpty() || QLineF(a0, a1).length() < 1e-6)
        return false;
    const QTransform t = toAxisFrame(a0, a1);
    QPainterPath all;
    for (int i = 0; i < hatchPaths.size(); ++i) {
        QPainterPath p = t.map(hatchPaths.at(i));
        p.setFillRule(Qt::OddEvenFill);
        all = all.isEmpty() ? p : all.united(p);
    }
    const QRectF br = all.boundingRect();
    if (br.isEmpty())
        return false;
    const double w = br.width() + 2.0;
    const double h = std::max(std::fabs(br.top()), std::fabs(br.bottom())) + 1.0;
    QPainterPath upper, lower;
    upper.addRect(QRectF(br.left() - 1.0, 0.0, w, h));
    lower.addRect(QRectF(br.left() - 1.0, -h, w, h));
    up = all.intersected(upper);
    QTransform mirror;
    mirror.scale(1.0, -1.0);
    loMirrored = mirror.map(all.intersected(lower));
    return true;
}
} // namespace

void RevolveProfile::sideInfo(const QList<QPainterPath> &hatchPaths, const QPointF &a0, const QPointF &a1,
                              bool &hasUpper, bool &hasLower, bool &symmetric)
{
    hasUpper = hasLower = false;
    symmetric = true;
    QPainterPath up, lo;
    if (!splitHalves(hatchPaths, a0, a1, up, lo))
        return;
    const double au = absAreaSum(up), al = absAreaSum(lo);
    hasUpper = au > 1e-6;
    hasLower = al > 1e-6;
    if (hasUpper && hasLower) {
        const double diff = absAreaSum(up.subtracted(lo)) + absAreaSum(lo.subtracted(up));
        symmetric = diff <= std::max(au, al) * 1e-4 + 1e-3;
    }
}

QList<RevolveLoop> RevolveProfile::sectionLoops(const QList<QPainterPath> &hatchPaths, const QPointF &a0,
                                                const QPointF &a1, int side)
{
    QList<RevolveLoop> loops;
    QPainterPath up, lo;
    if (!splitHalves(hatchPaths, a0, a1, up, lo))
        return loops;
    // 쪽 고르기: 지정이 없으면 넓은 쪽 (전단면이 대칭이면 아무 쪽이나 같다)
    if (side == SideLower || (side == SideAuto && absAreaSum(lo) > absAreaSum(up) * 1.000001))
        up = lo;

    const QList<QPolygonF> polys = up.toSubpathPolygons();
    QList<QPolygonF> clean;
    for (int i = 0; i < polys.size(); ++i) {
        const QPolygonF c = cleanLoop(polys.at(i));
        if (c.size() >= 3 && std::fabs(signedArea(c)) > 1e-6)
            clean << c;
    }
    // 안쪽 구멍 판정: 다른 고리 안에 든 횟수(홀수 = 구멍)
    for (int i = 0; i < clean.size(); ++i) {
        const QPolygonF &ci = clean.at(i);
        // 판정점: 첫 변 가운데에서 고리 안쪽으로 아주 조금
        QPointF probe = (ci.at(0) + ci.at(1)) * 0.5;
        int depth = 0;
        for (int j = 0; j < clean.size(); ++j)
            if (j != i && clean.at(j).containsPoint(probe, Qt::OddEvenFill) &&
                std::fabs(signedArea(clean.at(j))) > std::fabs(signedArea(ci)))
                ++depth;
        RevolveLoop lp;
        lp.hole = (depth % 2) == 1;
        lp.pts = ci;
        const double a = signedArea(ci);
        if ((!lp.hole && a < 0.0) || (lp.hole && a > 0.0))   // 바깥 = 반시계, 구멍 = 시계
            std::reverse(lp.pts.begin(), lp.pts.end());
        loops << lp;
    }
    return loops;
}

QList<RevolveCylinder> RevolveProfile::cylinders(const QList<RevolveLoop> &loops)
{
    QList<RevolveCylinder> raw;
    for (int i = 0; i < loops.size(); ++i) {
        const QPolygonF &p = loops.at(i).pts;
        for (int k = 0; k < p.size(); ++k) {
            const QPointF a = p.at(k), b = p.at((k + 1) % p.size());
            if (std::fabs(a.y() - b.y()) > 1e-5 || a.y() < 1e-4 || std::fabs(b.x() - a.x()) < 1e-3)
                continue;
            // 재료는 진행 방향 왼쪽: +x 로 가면 재료가 위(r 큰 쪽) → 구멍 면
            RevolveCylinder c;
            c.hole = b.x() > a.x();
            c.diameter = std::round((a.y() + b.y()) * 1e4) / 1e4;   // 지름 = 2r
            c.x0 = std::min(a.x(), b.x());
            c.x1 = std::max(a.x(), b.x());
            raw << c;
        }
    }
    std::sort(raw.begin(), raw.end(), cylinderLess);
    QList<RevolveCylinder> out;
    for (int i = 0; i < raw.size(); ++i) {
        const RevolveCylinder &c = raw.at(i);
        if (!out.isEmpty()) {
            RevolveCylinder &last = out.last();
            if (last.hole == c.hole && std::fabs(last.diameter - c.diameter) < 1e-6 && c.x0 <= last.x1 + 1e-6) {
                last.x1 = std::max(last.x1, c.x1);
                continue;
            }
        }
        out << c;
    }
    return out;
}

QList<RevolveCylinder> RevolveProfile::mergedByKey(const QList<RevolveCylinder> &cyls)
{
    QList<RevolveCylinder> out;
    for (int i = 0; i < cyls.size(); ++i) {
        bool merged = false;
        for (int j = 0; j < out.size(); ++j) {
            if (out.at(j).key() == cyls.at(i).key()) {
                out[j].x1 += cyls.at(i).x1 - cyls.at(i).x0;   // x0..x1 = 0..길이 합
                merged = true;
                break;
            }
        }
        if (!merged) {
            RevolveCylinder c = cyls.at(i);
            c.x1 = c.x1 - c.x0;
            c.x0 = 0.0;
            out << c;
        }
    }
    return out;
}

QString RevolveProfile::fitsToJson(const QMap<QString, QString> &fits)
{
    QJsonObject o;
    for (QMap<QString, QString>::const_iterator it = fits.constBegin(); it != fits.constEnd(); ++it)
        if (!it.value().trimmed().isEmpty())
            o.insert(it.key(), it.value().trimmed());
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QMap<QString, QString> RevolveProfile::fitsFromJson(const QString &json)
{
    QMap<QString, QString> out;
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    for (QJsonObject::const_iterator it = o.constBegin(); it != o.constEnd(); ++it)
        out.insert(it.key(), it.value().toString());
    return out;
}

bool RevolveProfile::taggedHatches(MyGraphicsScene *scene, const QString &uuid, QList<QPainterPath> &paths,
                                   QPointF &ref)
{
    paths.clear();
    if (!scene || uuid.isEmpty())
        return false;
    QRectF box;
    const QList<QGraphicsItem *> items = scene->items();
    for (int i = 0; i < items.size(); ++i) {
        const HatchItem *h = dynamic_cast<const HatchItem *>(items.at(i));
        if (!h || h->data(MECH_DATA_PART_ID).toString() != uuid)
            continue;
        const QPainterPath p = h->sceneTransform().map(h->geometryPath());
        paths << p;
        box = box.isNull() ? p.boundingRect() : box.united(p.boundingRect());
    }
    if (paths.isEmpty())
        return false;
    ref = QPointF(box.left(), box.top());
    return true;
}

QString RevolveProfile::findExisting(MyGraphicsScene *scene, const QList<QGraphicsItem *> &hatches,
                                     const QPointF &a0, const QPointF &a1, const QRectF &extraBox)
{
    if (!scene)
        return QString();
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    QRectF sel = extraBox;
    for (int i = 0; i < hatches.size(); ++i) {
        const QString id = hatches.at(i)->data(MECH_DATA_PART_ID).toString();
        if (!id.isEmpty() && reg->hasSymbol(id) && reg->symbolInfo(id).type == QLatin1String("MechRevolve"))
            return id;
        const HatchItem *h = dynamic_cast<const HatchItem *>(hatches.at(i));
        if (h) {
            const QRectF b = h->sceneTransform().map(h->geometryPath()).boundingRect();
            sel = sel.isNull() ? b : sel.united(b);
        }
    }
    const double ang = std::atan2(a1.y() - a0.y(), a1.x() - a0.x());
    const QMap<QString, SymbolInfo> &all = reg->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        if (it.value().type != QLatin1String("MechRevolve"))
            continue;
        QList<QPainterPath> paths;
        QPointF ref;
        if (!partPaths(scene, it.key(), paths, ref))
            continue;
        const QMap<QString, QVariant> &pr = it.value().props;
        const QPointF o(pr.value(QStringLiteral("ox")).toDouble() + ref.x() - pr.value(QStringLiteral("refX")).toDouble(),
                        pr.value(QStringLiteral("oy")).toDouble() + ref.y() - pr.value(QStringLiteral("refY")).toDouble());
        const double b = qDegreesToRadians(pr.value(QStringLiteral("angleDeg")).toDouble());
        if (std::fabs(std::sin(ang - b)) > 1e-6)
            continue;                                         // 축 방향이 다름
        const double dist = std::fabs(-(a0.x() - o.x()) * std::sin(b) + (a0.y() - o.y()) * std::cos(b));
        if (dist > 0.01)
            continue;                                         // 같은 축선이 아님
        QRectF box;
        for (int k = 0; k < paths.size(); ++k)
            box = box.isNull() ? paths.at(k).boundingRect() : box.united(paths.at(k).boundingRect());
        if (box.adjusted(-0.01, -0.01, 0.01, 0.01).intersects(sel))
            return it.key();
    }
    return QString();
}

QString RevolveProfile::registerRevolve(MyGraphicsScene *scene, QUndoStack *undo,
                                        const QList<QGraphicsItem *> &hatches, const QPointF &a0in,
                                        const QPointF &a1in, const QString &namePrefix,
                                        const QMap<QString, QString> &fits, int side, const QString &existingUuid,
                                        QString *error)
{
    QPointF a0 = a0in, a1 = a1in;
    normalizeAxis(a0, a1);
    if (!scene || hatches.isEmpty()) {
        if (error)
            *error = QStringLiteral("해칭이 없습니다.");
        return QString();
    }
    QList<QPainterPath> paths;
    QRectF box;
    for (int i = 0; i < hatches.size(); ++i) {
        const HatchItem *h = dynamic_cast<const HatchItem *>(hatches.at(i));
        if (!h) {
            if (error)
                *error = QStringLiteral("해칭이 아닌 객체가 섞여 있습니다.");
            return QString();
        }
        const QPainterPath p = h->sceneTransform().map(h->geometryPath());
        paths << p;
        box = box.isNull() ? p.boundingRect() : box.united(p.boundingRect());
    }
    const QList<RevolveLoop> loops = sectionLoops(paths, a0, a1, side);
    if (loops.isEmpty()) {
        if (error)
            *error = QStringLiteral("해칭에서 회전 단면을 만들 수 없습니다. 회전축(중심선)이 해칭 옆을 지나는지 확인하세요.");
        return QString();
    }
    const double angleDeg = std::atan2(a1.y() - a0.y(), a1.x() - a0.x()) * 180.0 / M_PI;
    const QPointF ref(box.left(), box.top());

    // 기존 회전체 고치기: 옛 해칭 태그 떼고 새 해칭에 붙이고, 축·끼워맞춤·쪽 갱신
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    if (!existingUuid.isEmpty() && reg->hasSymbol(existingUuid)) {
        const SymbolInfo oldInfo = reg->symbolInfo(existingUuid);
        SymbolInfo info = oldInfo;
        info.props[QStringLiteral("ox")] = a0.x();
        info.props[QStringLiteral("oy")] = a0.y();
        info.props[QStringLiteral("angleDeg")] = angleDeg;
        info.props[QStringLiteral("refX")] = ref.x();
        info.props[QStringLiteral("refY")] = ref.y();
        info.props[QStringLiteral("Fits")] = fitsToJson(fits);
        info.props[QStringLiteral("Side")] = side;
        info.props.remove(QStringLiteral("Source"));
        info.props.remove(QStringLiteral("Box"));
        info.props.remove(QStringLiteral("Picks"));
        info.props.remove(QStringLiteral("Region"));
        if (namePrefix != oldInfo.props.value(QStringLiteral("Name")).toString()) {
            info.props[QStringLiteral("Name")] = namePrefix;
            info.name = namePrefix + QStringLiteral("-1");
        }
        if (undo)
            undo->push(new UpdateRegistryCommand(reg, existingUuid, oldInfo, info));
        else
            reg->registerSymbol(existingUuid, info);
        const QList<QGraphicsItem *> items = scene->items();
        for (int i = 0; i < items.size(); ++i)
            if (items.at(i)->data(MECH_DATA_PART_ID).toString() == existingUuid && !hatches.contains(items.at(i)))
                items.at(i)->setData(MECH_DATA_PART_ID, QVariant());
        for (int i = 0; i < hatches.size(); ++i)
            hatches.at(i)->setData(MECH_DATA_PART_ID, existingUuid);
        return existingUuid;
    }

    QMap<QString, QVariant> props;
    props[QStringLiteral("Name")] = namePrefix;
    props[QStringLiteral("ax1x")] = a1.x();
    props[QStringLiteral("ax1y")] = a1.y();
    props[QStringLiteral("Fits")] = fitsToJson(fits);
    props[QStringLiteral("Side")] = side;
    const QString uuid = MechPartTag::registerPart(scene, undo, hatches.first(), QStringLiteral("MechRevolve"),
                                                   namePrefix, props, a0, angleDeg, &ref);
    if (uuid.isEmpty()) {
        if (error)
            *error = QStringLiteral("부품 등록에 실패했습니다.");
        return QString();
    }
    for (int i = 1; i < hatches.size(); ++i)
        hatches.at(i)->setData(MECH_DATA_PART_ID, uuid);
    return uuid;
}

// ─────────────────────────────────────────────────────────────────────────────
// 영역(Area) 방식
// ─────────────────────────────────────────────────────────────────────────────
namespace {
QJsonArray pointsJson(const QList<QPointF> &pts)
{
    QJsonArray a;
    for (int i = 0; i < pts.size(); ++i)
        a.append(QJsonArray{pts.at(i).x(), pts.at(i).y()});
    return a;
}

QString regionJson(const QPainterPath &path)
{
    QJsonArray polys;
    const QList<QPolygonF> ps = path.toSubpathPolygons();
    for (int i = 0; i < ps.size(); ++i) {
        QJsonArray poly;
        for (int k = 0; k < ps.at(i).size(); ++k)
            poly.append(QJsonArray{ps.at(i).at(k).x(), ps.at(i).at(k).y()});
        polys.append(poly);
    }
    return QString::fromUtf8(QJsonDocument(polys).toJson(QJsonDocument::Compact));
}

QPainterPath regionFromJson(const QString &json)
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    const QJsonArray polys = QJsonDocument::fromJson(json.toUtf8()).array();
    for (int i = 0; i < polys.size(); ++i) {
        const QJsonArray poly = polys.at(i).toArray();
        QPolygonF pg;
        for (int k = 0; k < poly.size(); ++k)
            pg << QPointF(poly.at(k).toArray().at(0).toDouble(), poly.at(k).toArray().at(1).toDouble());
        if (pg.size() >= 3)
            path.addPolygon(pg);
    }
    return path;
}
} // namespace

namespace {
// 다각형 변까지 가장 가까운 거리
double distToEdges(const QPointF &p, const QList<QPolygonF> &polys)
{
    double best = 1e300;
    for (int i = 0; i < polys.size(); ++i) {
        const QPolygonF &pg = polys.at(i);
        for (int k = 0; k + 1 < pg.size(); ++k) {
            const QPointF a = pg.at(k), b = pg.at(k + 1);
            const QPointF ab = b - a;
            const double L2 = ab.x() * ab.x() + ab.y() * ab.y();
            double t = L2 > 1e-18 ? ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / L2 : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            const QPointF q = a + ab * t;
            best = std::min(best, std::hypot(p.x() - q.x(), p.y() - q.y()));
        }
    }
    return best;
}
} // namespace

namespace {
// 경계 사각 안에 그려진 구멍 단면들이 차지하는 자리 (구멍 단면 선의 태그 → 원본 구멍 스펙으로 다시 계산)
//   해치 방식 부품은 투상도를 만들 때 해칭에서 구멍 자리를 뺀다. 선 영역을 못 찾아 해칭 모양으로 대신할 때
//   이 자리를 다시 채우지 않으면 단면에 구멍 홈이 생겨, 회전체 3D·측면도에 구멍끼리 잇는 고리(원)가 생긴다.
QPainterPath holeSectionArea(MyGraphicsScene *scene, const QRectF &box)
{
    QPainterPath area;
    // 원본 구멍 스펙 (HOLE_DATA_GROUP_ID → HoleItem)
    QHash<QString, MyHole> specs;
    const QList<QGraphicsItem *> all = scene->items();
    for (int i = 0; i < all.size(); ++i) {
        const HoleItem *hi = dynamic_cast<const HoleItem *>(all.at(i));
        if (!hi)
            continue;
        const QString gid = hi->data(HOLE_DATA_GROUP_ID).toString();
        if (!gid.isEmpty() && !specs.contains(gid))
            specs.insert(gid, hi->holeData());
    }
    QSet<QString> seen;
    const QList<QGraphicsItem *> items = scene->items(box, Qt::IntersectsItemShape);
    for (int i = 0; i < items.size(); ++i) {
        const QGraphicsItem *gi = items.at(i);
        const QString gid = gi->data(HOLE_SECTION_GROUP_ID).toString();
        if (gid.isEmpty() || !specs.contains(gid))
            continue;
        const QPointF top(gi->data(HOLE_SECTION_TOP_X).toDouble(), gi->data(HOLE_SECTION_TOP_Y).toDouble());
        QPointF dir(gi->data(HOLE_SECTION_DIR_X).toDouble(), gi->data(HOLE_SECTION_DIR_Y).toDouble());
        const double dl = std::hypot(dir.x(), dir.y());
        if (dl < 1e-9)
            continue;
        dir /= dl;
        // 한 구멍 단면 = 같은 원본·입구·방향의 선 여러 개 → 한 번만
        const QString key = QStringLiteral("%1|%2|%3|%4|%5").arg(gid).arg(top.x(), 0, 'f', 4).arg(top.y(), 0, 'f', 4)
                                .arg(dir.x(), 0, 'f', 4).arg(dir.y(), 0, 'f', 4);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        HoleFeature tmp;
        tmp.hole = specs.value(gid);
        const QPainterPath p = tmp.frontArea(top, dir, gi->data(HOLE_SECTION_MATERIAL_THICKNESS).toDouble());
        if (!p.isEmpty())
            area = area.isEmpty() ? p : area.united(p);
    }
    return area;
}

// 구멍 자리 p 가 해칭에 닿는가 (해칭에서 뺀 자리면 경계가 딱 맞닿음 → 살짝 넓혀서 판정)
bool touchesHatch(const QPainterPath &hatchPath, const QPainterPath &p)
{
    QPainterPathStroker st;
    st.setWidth(0.2);
    return hatchPath.intersects(p.united(st.createStroke(p)));
}

// 선 영역 대신 해칭 모양을 쓸 때: 해칭에서 뺀 구멍 자리를 다시 채운 모양
QPainterPath hatchWithHoles(MyGraphicsScene *scene, const QPainterPath &hatchPath)
{
    const QRectF hb = hatchPath.boundingRect();
    const QPainterPath holes = holeSectionArea(scene, hb);
    if (holes.isEmpty())
        return hatchPath;
    // 해칭에 닿는 구멍 자리만 (다른 부품의 구멍은 제외), 해칭 경계 사각 안으로 자름
    QPainterPath fill;
    const QList<QPolygonF> hp = holes.toFillPolygons();
    for (int i = 0; i < hp.size(); ++i) {
        QPainterPath one;
        one.addPolygon(hp.at(i));
        if (touchesHatch(hatchPath, one))
            fill = fill.isEmpty() ? one : fill.united(one);
    }
    if (fill.isEmpty())
        return hatchPath;
    QPainterPath clip;
    clip.addRect(hb);
    QPainterPath out = hatchPath.united(fill.intersected(clip));
    out.setFillRule(Qt::OddEvenFill);
    return out;
}
} // namespace

QPainterPath RevolveProfile::hatchLineRegion(MyGraphicsScene *scene, const QPainterPath &hatchPath)
{
    if (!scene || hatchPath.isEmpty())
        return hatchPath;
    const QRectF hb = hatchPath.boundingRect();
    const QList<QPolygonF> polys = hatchPath.toFillPolygons();
    // 해칭 조각마다 안쪽 점 하나 (변에서 가장 먼 격자점 — 경계 찾기가 안정)
    QList<QPointF> picks;
    for (int i = 0; i < polys.size(); ++i) {
        const QRectF pb = polys.at(i).boundingRect();
        if (pb.width() < 1e-6 || pb.height() < 1e-6)
            continue;
        const int N = 24;
        QPointF bestPt;
        double bestD = -1.0;
        for (int a = 1; a < N; ++a) {
            for (int b = 1; b < N; ++b) {
                const QPointF q(pb.left() + pb.width() * a / N, pb.top() + pb.height() * b / N);
                if (!polys.at(i).containsPoint(q, Qt::OddEvenFill) || !hatchPath.contains(q))
                    continue;                                   // 해칭 안쪽만 (구멍·섬 제외)
                const double d = distToEdges(q, polys);
                if (d > bestD) {
                    bestD = d;
                    bestPt = q;
                }
            }
        }
        if (bestD > 0.0)
            picks << bestPt;
    }
    if (picks.isEmpty())
        return hatchWithHoles(scene, hatchPath);
    QPainterPath region = areaRegion(scene, hb, picks, false, true);
    if (region.isEmpty())
        return hatchWithHoles(scene, hatchPath);
    // 선 영역이 해칭과 같은 자리인지 (경계 사각이 거의 같아야 함 — 구멍을 빼도 바깥 경계는 그대로)
    const QRectF rb = region.boundingRect();
    const double tol = std::max(0.5, 0.01 * std::hypot(hb.width(), hb.height()));
    if (std::fabs(rb.left() - hb.left()) > tol || std::fabs(rb.right() - hb.right()) > tol ||
        std::fabs(rb.top() - hb.top()) > tol || std::fabs(rb.bottom() - hb.bottom()) > tol)
        return hatchWithHoles(scene, hatchPath);
    region.setFillRule(Qt::OddEvenFill);
    return region;
}

QPainterPath RevolveProfile::areaRegion(MyGraphicsScene *scene, const QRectF &box, const QList<QPointF> &picks,
                                        bool includeViewLines, bool excludeHoleSections, bool excludeHoles,
                                        const QStringList &excludeViewIds)
{
    QPainterPath out;
    if (!scene || box.isEmpty())
        return out;
    // 해치 명령과 같은 후보: 사각에 걸치는 도형 (해칭·투상도 선은 제외)
    QList<CadItem *> items;
    // 내부 점 방식은 점을 둘러싼 가장 작은 공간을 찾으므로 후보를 넓게 잡아도 안전하다
    //   → 선을 사각 밖으로 늘려도(고쳐도) 다시 찾을 수 있게 사각 크기만큼 사방으로 넓힌다
    const double grow = picks.isEmpty() ? 0.0 : std::max(box.width(), box.height());
    const QList<QGraphicsItem *> gi =
        scene->items(box.adjusted(-grow, -grow, grow, grow), Qt::IntersectsItemShape);
    for (int i = 0; i < gi.size(); ++i) {
        CadItem *ci = dynamic_cast<CadItem *>(gi.at(i));
        if (!ci || dynamic_cast<HatchItem *>(ci) || !ci->isVisible())
            continue;
        const QString vid = ci->data(MECH_VIEW_ID).toString();
        if (!vid.isEmpty() && (!includeViewLines || excludeViewIds.contains(vid)))
            continue;
        if (excludeHoleSections && !ci->data(HOLE_SECTION_GROUP_ID).toString().isEmpty())
            continue;
        // 구멍 객체와 그 중심선(같은 HOLE_DATA_GROUP_ID 의 독립 선) — 판을 가로질러 공간을 쪼개지 않게
        if (excludeHoles && (dynamic_cast<HoleItem *>(ci) || !ci->data(HOLE_DATA_GROUP_ID).toString().isEmpty()))
            continue;
        items.append(ci);
    }
    if (items.isEmpty())
        return out;
    if (picks.isEmpty())
        return AdvancedBoundaryFinder::findBoundaryPathInRect(items, box);   // 복합모양
    for (int i = 0; i < picks.size(); ++i) {
        QPainterPath p = AdvancedBoundaryFinder::findBoundaryPath(items, picks.at(i));
        if (p.isEmpty())
            continue;
        p.setFillRule(Qt::OddEvenFill);
        out = out.isEmpty() ? p : out.united(p);
    }
    return out;
}

bool RevolveProfile::partPaths(MyGraphicsScene *scene, const QString &uuid, QList<QPainterPath> &paths, QPointF &ref)
{
    paths.clear();
    if (!scene || !scene->symbolRegistry()->hasSymbol(uuid))
        return false;
    const SymbolInfo info = scene->symbolRegistry()->symbolInfo(uuid);
    if (info.props.value(QStringLiteral("Source")).toString() != QLatin1String("area")) {
        // 해치 방식: 해칭은 위치(기준점)만, 단면은 해칭이 덮는 선 영역 → 해칭에서 구멍 자리를 빼도 3D 단면은 그대로
        if (!taggedHatches(scene, uuid, paths, ref))
            return false;
        for (int i = 0; i < paths.size(); ++i)
            paths[i] = hatchLineRegion(scene, paths.at(i));
        return true;
    }

    const QMap<QString, QVariant> &pr = info.props;
    const QJsonArray b = QJsonDocument::fromJson(pr.value(QStringLiteral("Box")).toString().toUtf8()).array();
    const QRectF box(b.at(0).toDouble(), b.at(1).toDouble(), b.at(2).toDouble(), b.at(3).toDouble());
    QList<QPointF> picks;
    const QJsonArray pj = QJsonDocument::fromJson(pr.value(QStringLiteral("Picks")).toString().toUtf8()).array();
    for (int i = 0; i < pj.size(); ++i)
        picks << QPointF(pj.at(i).toArray().at(0).toDouble(), pj.at(i).toArray().at(1).toDouble());
    // 구멍 단면 선(측면도 구멍 연동·구멍 단면 도구)은 경계가 아님 — 구멍은 커터로 따로 판다.
    //   경계로 잡히면 단면에 홈이 생기고, 회전하면 끝면이 고리 모양으로 뚫린다 (해치 방식 hatchLineRegion 과 같은 규칙)
    QPainterPath region = areaRegion(scene, box, picks, false, true);
    if (region.isEmpty())
        region = regionFromJson(pr.value(QStringLiteral("Region")).toString());   // 못 찾으면 등록 때 모양
    if (region.isEmpty())
        return false;
    paths << region;
    ref = QPointF(pr.value(QStringLiteral("refX")).toDouble(), pr.value(QStringLiteral("refY")).toDouble());
    return true;
}

QString RevolveProfile::registerRevolveArea(MyGraphicsScene *scene, QUndoStack *undo, const QRectF &box,
                                            const QList<QPointF> &picks, const QPainterPath &region,
                                            const QPointF &a0in, const QPointF &a1in, const QString &namePrefix,
                                            const QMap<QString, QString> &fits, int side,
                                            const QString &existingUuid, QString *error)
{
    QPointF a0 = a0in, a1 = a1in;
    normalizeAxis(a0, a1);
    if (!scene || region.isEmpty()) {
        if (error)
            *error = QStringLiteral("닫힌 공간을 찾지 못했습니다.");
        return QString();
    }
    if (sectionLoops(QList<QPainterPath>{region}, a0, a1, side).isEmpty()) {
        if (error)
            *error = QStringLiteral("고른 공간에서 회전 단면을 만들 수 없습니다. 회전축(중심선)이 공간 옆을 지나는지 확인하세요.");
        return QString();
    }
    const double angleDeg = std::atan2(a1.y() - a0.y(), a1.x() - a0.x()) * 180.0 / M_PI;
    const QPointF ref = region.boundingRect().topLeft();   // 영역 방식은 이동을 따라가지 않으므로 고정값

    QMap<QString, QVariant> props;
    props[QStringLiteral("Name")] = namePrefix;
    props[QStringLiteral("Fits")] = fitsToJson(fits);
    props[QStringLiteral("Side")] = side;
    props[QStringLiteral("Source")] = QStringLiteral("area");
    props[QStringLiteral("Box")] = QString::fromUtf8(
        QJsonDocument(QJsonArray{box.x(), box.y(), box.width(), box.height()}).toJson(QJsonDocument::Compact));
    props[QStringLiteral("Picks")] = QString::fromUtf8(QJsonDocument(pointsJson(picks)).toJson(QJsonDocument::Compact));
    props[QStringLiteral("Region")] = regionJson(region);

    SceneSymbolRegistry *reg = scene->symbolRegistry();
    if (!existingUuid.isEmpty() && reg->hasSymbol(existingUuid)) {
        const SymbolInfo oldInfo = reg->symbolInfo(existingUuid);
        SymbolInfo info = oldInfo;
        for (QMap<QString, QVariant>::const_iterator it = props.constBegin(); it != props.constEnd(); ++it)
            info.props[it.key()] = it.value();
        info.props[QStringLiteral("ox")] = a0.x();
        info.props[QStringLiteral("oy")] = a0.y();
        info.props[QStringLiteral("angleDeg")] = angleDeg;
        info.props[QStringLiteral("refX")] = ref.x();
        info.props[QStringLiteral("refY")] = ref.y();
        if (namePrefix != oldInfo.props.value(QStringLiteral("Name")).toString())
            info.name = namePrefix + QStringLiteral("-1");
        if (undo)
            undo->push(new UpdateRegistryCommand(reg, existingUuid, oldInfo, info));
        else
            reg->registerSymbol(existingUuid, info);
        // 해칭 방식이었다면 옛 해칭 태그를 뗀다
        const QList<QGraphicsItem *> items = scene->items();
        for (int i = 0; i < items.size(); ++i)
            if (items.at(i)->data(MECH_DATA_PART_ID).toString() == existingUuid)
                items.at(i)->setData(MECH_DATA_PART_ID, QVariant());
        return existingUuid;
    }
    const QString uuid = MechPartTag::registerPart(scene, undo, nullptr, QStringLiteral("MechRevolve"), namePrefix,
                                                   props, a0, angleDeg, &ref);
    if (uuid.isEmpty() && error)
        *error = QStringLiteral("부품 등록에 실패했습니다.");
    return uuid;
}
