#include "extrudefeature.h"

#include "mechmodel3d.h"
#include "mechparttag.h"
#include "mechpartdata.h"
#include "mygraphicsscene.h"
#include "plateview.h"
#include "projectionview.h"
#include "revolveprofile.h"
#include "scenesymbolregistry.h"

#include <QGraphicsItem>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QUndoStack>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {

double ringArea(const QPolygonF &p)
{
    double a = 0.0;
    for (int i = 0; i < p.size(); ++i) {
        const QPointF &u = p.at(i), &v = p.at((i + 1) % p.size());
        a += u.x() * v.y() - v.x() * u.y();
    }
    return a * 0.5;
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

// 고리 방향 맞춤: 바깥 = 반시계, 안쪽 구멍 = 시계 (다른 고리 안에 든 횟수)
QList<QPolygonF> orientRings(const QList<QPolygonF> &rings)
{
    QList<QPolygonF> out;
    for (int i = 0; i < rings.size(); ++i) {
        int depthN = 0;
        const QPointF probe = (rings.at(i).at(0) + rings.at(i).at(1)) * 0.5;
        for (int j = 0; j < rings.size(); ++j)
            if (j != i && std::fabs(ringArea(rings.at(j))) > std::fabs(ringArea(rings.at(i))) &&
                rings.at(j).containsPoint(probe, Qt::OddEvenFill))
                ++depthN;
        QPolygonF r = rings.at(i);
        const bool hole = (depthN % 2) == 1;
        if ((!hole && ringArea(r) < 0.0) || (hole && ringArea(r) > 0.0))
            std::reverse(r.begin(), r.end());
        out << r;
    }
    return out;
}

// 경로 → 닫힌 고리들 (겹친 끝점·짧은 변 정리)
QList<QPolygonF> cleanRings(const QPainterPath &region)
{
    QList<QPolygonF> rings;
    const QList<QPolygonF> polys = region.toSubpathPolygons();
    for (int i = 0; i < polys.size(); ++i) {
        QPolygonF r;
        for (int k = 0; k < polys.at(i).size(); ++k) {
            const QPointF q = polys.at(i).at(k);
            if (!r.isEmpty() && QLineF(r.last(), q).length() < 1e-6)
                continue;
            r << q;
        }
        while (r.size() > 1 && QLineF(r.first(), r.last()).length() < 1e-6)
            r.removeLast();
        if (r.size() >= 3 && std::fabs(ringArea(r)) > 1e-6)
            rings << r;
    }
    return rings;
}

// 두 사각이 (거의) 같은가 — 모서리 차이가 tol 이하
bool sameRect(const QRectF &a, const QRectF &b, double tol)
{
    return std::fabs(a.left() - b.left()) < tol && std::fabs(a.top() - b.top()) < tol &&
           std::fabs(a.right() - b.right()) < tol && std::fabs(a.bottom() - b.bottom()) < tol;
}

// 두 사각 모서리 차이의 합 (등록 때 모양과 얼마나 다른가)
double rectDiff(const QRectF &a, const QRectF &b)
{
    return std::fabs(a.left() - b.left()) + std::fabs(a.top() - b.top()) + std::fabs(a.right() - b.right()) +
           std::fabs(a.bottom() - b.bottom());
}

// 공간 다시 찾기 + 고른 점 보정
//   저장된 고른 점(picks)으로 공간을 다시 찾되, 나중에 그 점 위에 다른 닫힌 모양(예: 판 안쪽에 그린 다각형)을
//   그리면 고른 점이 그 작은 모양 안에 들어가 "판 = 안쪽 다각형" 으로 바뀌어 버린다.
//   → 다시 찾은 공간이 등록 때 모양(Region)보다 눈에 띄게 작으면, 등록 모양에서 새 공간을 뺀 나머지 안의 점으로
//     다시 골라 보고, 등록 모양에 더 가까우면 그것을 쓴다 (새 모양은 섬 = 구멍 고리가 됨).
namespace {
// 공간 찾기 (plateFlags = 단독 돌출 규칙: 구멍 단면 선·구멍 객체는 경계가 아님)
QPainterPath findArea(MyGraphicsScene *scene, const QRectF &box, const QList<QPointF> &picks,
                      const QStringList &excludeViewIds, bool plateFlags)
{
    return RevolveProfile::areaRegion(scene, box, picks, true, plateFlags, plateFlags, excludeViewIds);
}

double rectArea(const QRectF &r) { return std::fabs(r.width() * r.height()); }

// 경로 안쪽의 점 하나 (격자 표본, 둘레 여유 = 칸 크기의 1/4). 없으면 false
bool interiorPoint(const QPainterPath &area, QPointF &out)
{
    const QRectF bb = area.boundingRect();
    const int n = 24;
    const double cw = bb.width() / n, ch = bb.height() / n;
    const double m = 0.25 * std::min(cw, ch);
    if (m <= 0.0)
        return false;
    for (int iy = 0; iy < n; ++iy)
        for (int ix = 0; ix < n; ++ix) {
            const QPointF p(bb.left() + (ix + 0.5) * cw, bb.top() + (iy + 0.5) * ch);
            if (area.contains(QRectF(p.x() - m, p.y() - m, 2.0 * m, 2.0 * m))) {
                out = p;
                return true;
            }
        }
    return false;
}
} // namespace

// 공간 다시 찾기 + 고른 점 보정
//   저장된 고른 점(picks)으로 공간을 다시 찾되,
//   ① 나중에 그 점 위에 다른 닫힌 모양(예: 판 안쪽에 그린 다각형)을 그리면 고른 점이 그 작은 모양 안에 들어가
//      "판 = 안쪽 다각형" 으로 바뀌어 버린다.
//   ② 모양을 지우거나 옮겨 고른 점이 빈 곳에 떨어지면 도면틀 안쪽 같은 엉뚱한 큰 공간이 잡힌다
//      (받침대·판이 도면틀만 하게 커지던 문제).
//   → 다시 찾은 공간이 등록 때 모양(Region)과 다르면 등록 모양 안의 점(①은 덮지 않는 부분)으로 다시 골라
//     등록 모양에 더 가까운 쪽을 쓰고, 그래도 등록 모양보다 4배 넘게 크면 그 모양은 없어진 것으로 본다(빈 경로).
QPainterPath repickRegion(MyGraphicsScene *scene, const QRectF &box, const QList<QPointF> &picks,
                          const QString &storedJson, const QStringList &excludeViewIds, bool plateFlags)
{
    QPainterPath region = findArea(scene, box, picks, excludeViewIds, plateFlags);
    const QPainterPath stored = regionFromJson(storedJson);
    if (stored.isEmpty() || region.isEmpty())
        return region;
    const QRectF sb = stored.boundingRect();
    QRectF rb = region.boundingRect();
    const double tol = std::max(1e-3, 1e-4 * std::max(sb.width(), sb.height()));
    if (sameRect(sb, rb, tol))
        return region;                                  // 등록 때와 같은 바깥 경계 — 그대로
    const bool tooBig = rectArea(rb) > 4.0 * rectArea(sb);
    QPainterPath sample = stored.subtracted(region);    // ① 등록 모양 중 지금 공간이 덮지 않는 부분
    if (sample.isEmpty()) {
        if (!tooBig)
            return region;                              // 선을 고쳐 조금 커진 것 — 그대로
        sample = stored;                                // ② 등록 모양 안 아무 점
    }
    QPointF p;
    if (interiorPoint(sample, p)) {
        const QPainterPath alt = findArea(scene, box, QList<QPointF>{p}, excludeViewIds, plateFlags);
        if (!alt.isEmpty() && rectDiff(alt.boundingRect(), sb) < rectDiff(rb, sb)) {
            region = alt;                               // 등록 모양에 더 가까움 → 이 공간
            rb = region.boundingRect();
        }
    }
    if (rectArea(rb) > 4.0 * rectArea(sb))
        return QPainterPath();                          // 등록 모양을 다시 찾지 못함 (지워졌거나 옮겨짐)
    return region;
}

// 회전체에 붙은 돌출(측면도에 그린 공간)의 현재 공간 (도면 좌표)
//   · 고른 점·사각·등록 모양은 등록 때 절대 좌표 → 등록 때 측면도 중심(ViewCx, ViewCy)에서 지금 중심까지 옮겨서 씀
//     (측면도와 스케치를 함께 옮겨도 따라감. 옛 기록은 중심이 없어 그대로)
//   · 경계 = 그린 측면도의 투상 선 + 사용자 선. 다른 뷰(평면도·보조 투상도·정면 구멍선·다른 부품 뷰) 선은 뺀다
//     — 그리는 중인 새 뷰 선이 아직 기록 전이어도 빠지도록 사각 근처 객체의 뷰 태그로 고름
//   · 다시 못 찾으면(고른 점이 빈 곳) 등록 모양. stale = 다시 못 찾았음
QPainterPath hostExtrudeRegion(MyGraphicsScene *scene, const QMap<QString, QVariant> &pr, const QString &viewId,
                               const QPointF &svCenter, bool *stale)
{
    QPointF shift(0.0, 0.0);
    if (pr.contains(QStringLiteral("ViewCx")))
        shift = svCenter - QPointF(pr.value(QStringLiteral("ViewCx")).toDouble(), pr.value(QStringLiteral("ViewCy")).toDouble());
    const QJsonArray b = QJsonDocument::fromJson(pr.value(QStringLiteral("Box")).toString().toUtf8()).array();
    const QRectF box =
        QRectF(b.at(0).toDouble(), b.at(1).toDouble(), b.at(2).toDouble(), b.at(3).toDouble()).translated(shift);
    QList<QPointF> picks;
    const QJsonArray pj = QJsonDocument::fromJson(pr.value(QStringLiteral("Picks")).toString().toUtf8()).array();
    for (int i = 0; i < pj.size(); ++i)
        picks << QPointF(pj.at(i).toArray().at(0).toDouble(), pj.at(i).toArray().at(1).toDouble()) + shift;
    const QPainterPath stored = regionFromJson(pr.value(QStringLiteral("Region")).toString()).translated(shift);
    // 사각 근처(areaRegion 과 같은 넓힘)의 다른 뷰 태그
    QStringList otherViews;
    {
        const double grow = picks.isEmpty() ? 0.0 : std::max(box.width(), box.height());
        const QList<QGraphicsItem *> near =
            scene->items(box.adjusted(-grow, -grow, grow, grow), Qt::IntersectsItemShape);
        QSet<QString> ids;
        for (int i = 0; i < near.size(); ++i) {
            const QString v = near.at(i)->data(MECH_VIEW_ID).toString();
            if (!v.isEmpty() && v != viewId)
                ids.insert(v);
        }
        for (QSet<QString>::const_iterator it = ids.constBegin(); it != ids.constEnd(); ++it)
            otherViews << *it;
    }
    QPainterPath region = repickRegion(scene, box, picks, regionJson(stored), otherViews, false);
    if (stale)
        *stale = region.isEmpty();
    if (region.isEmpty())
        region = stored;
    return region;
}

// 회전체 host 의 viewId 측면도 현재 중심. 없으면 false
bool sideViewCenter(MyGraphicsScene *scene, const QString &host, const QString &viewId, QPointF &c)
{
    const QList<ProjectionView::SideViewInfo> svs = ProjectionView::sideViews(scene, host);
    for (int i = 0; i < svs.size(); ++i)
        if (svs.at(i).viewId == viewId) {
            c = svs.at(i).center;
            return true;
        }
    return false;
}

// 다른 단독 돌출들의 공간 경계 사각 (판 해석 중 부름 — resolve 를 부르지 않아 서로 되부르지 않음)
//   판 안쪽에 그린 모양이 따로 돌출된 것이면 그 섬은 판의 관통 구멍이 아님 (판 위에 올린 돌출)
QList<QRectF> otherStandaloneBoxes(MyGraphicsScene *scene, const QString &selfUuid)
{
    QList<QRectF> out;
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        const QMap<QString, QVariant> &p = it.value().props;
        if (it.key() == selfUuid || it.value().type != QLatin1String("MechExtrude") ||
            !p.value(QStringLiteral("Standalone")).toBool() || p.value(QStringLiteral("PlateFront")).toBool())
            continue;
        const QJsonArray b = QJsonDocument::fromJson(p.value(QStringLiteral("Box")).toString().toUtf8()).array();
        const QRectF box(b.at(0).toDouble(), b.at(1).toDouble(), b.at(2).toDouble(), b.at(3).toDouble());
        QList<QPointF> picks;
        const QJsonArray pj = QJsonDocument::fromJson(p.value(QStringLiteral("Picks")).toString().toUtf8()).array();
        for (int i = 0; i < pj.size(); ++i)
            picks << QPointF(pj.at(i).toArray().at(0).toDouble(), pj.at(i).toArray().at(1).toDouble());
        QStringList views;
        const QJsonArray vj = QJsonDocument::fromJson(p.value(QStringLiteral("Views")).toString().toUtf8()).array();
        for (int i = 0; i < vj.size(); ++i)
            views << vj.at(i).toObject().value(QStringLiteral("id")).toString();
        const QPainterPath region =
            repickRegion(scene, box, picks, p.value(QStringLiteral("Region")).toString(), views, true);
        if (!region.isEmpty())
            out << region.boundingRect();
    }
    return out;
}

// 판(단독 돌출)의 투상도 뷰 UUID 들 — 그 선은 판·파내기 공간의 경계가 아님
QStringList plateViewIds(MyGraphicsScene *scene, const QString &plateUuid)
{
    QStringList ids;
    if (!scene || !scene->symbolRegistry()->hasSymbol(plateUuid))
        return ids;
    const QJsonArray vj = QJsonDocument::fromJson(
                              scene->symbolRegistry()->symbolInfo(plateUuid).props.value(QStringLiteral("Views")).toString().toUtf8())
                              .array();
    for (int i = 0; i < vj.size(); ++i)
        ids << vj.at(i).toObject().value(QStringLiteral("id")).toString();
    return ids;
}

// 이 판에 붙은 정면 파내기들의 공간 경계 사각 (판 해석 중 부름 — 판을 다시 해석하지 않음)
QList<QRectF> frontCutBoxes(MyGraphicsScene *scene, const QString &plateUuid, const QStringList &ownViews)
{
    QList<QRectF> out;
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        const QMap<QString, QVariant> &p = it.value().props;
        if (it.value().type != QLatin1String("MechExtrude") || !p.value(QStringLiteral("PlateFront")).toBool() ||
            p.value(QStringLiteral("Host")).toString() != plateUuid)
            continue;
        const QJsonArray b = QJsonDocument::fromJson(p.value(QStringLiteral("Box")).toString().toUtf8()).array();
        const QRectF box(b.at(0).toDouble(), b.at(1).toDouble(), b.at(2).toDouble(), b.at(3).toDouble());
        QList<QPointF> picks;
        const QJsonArray pj = QJsonDocument::fromJson(p.value(QStringLiteral("Picks")).toString().toUtf8()).array();
        for (int i = 0; i < pj.size(); ++i)
            picks << QPointF(pj.at(i).toArray().at(0).toDouble(), pj.at(i).toArray().at(1).toDouble());
        const QPainterPath region = RevolveProfile::areaRegion(scene, box, picks, true, true, true, ownViews);
        if (!region.isEmpty())
            out << region.boundingRect();
    }
    return out;
}

} // namespace

void ExtrudeFeature::removeSameStandalone(MyGraphicsScene *scene, QUndoStack *undo, const QPainterPath &region)
{
    if (!scene || region.isEmpty())
        return;
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    const QMap<QString, SymbolInfo> all = reg->allSymbols();   // 복사 (도는 중에 지움)
    const double tiny = 1e-3;
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        if (it.value().type != QLatin1String("MechExtrude") || !it.value().props.value(QStringLiteral("Standalone")).toBool())
            continue;
        ExtrudeInfo ei;
        if (!resolve(scene, it.key(), ei))
            continue;
        QPainterPath old;
        old.setFillRule(Qt::OddEvenFill);
        for (int i = 0; i < ei.rings.size(); ++i) {
            QPolygonF pg = ei.rings.at(i);
            pg << pg.first();
            old.addPolygon(pg);
        }
        // 두 모양의 차이가 (거의) 없으면 같은 공간
        const QRectF d1 = old.subtracted(region).boundingRect();
        const QRectF d2 = region.subtracted(old).boundingRect();
        if ((d1.width() >= tiny && d1.height() >= tiny) || (d2.width() >= tiny && d2.height() >= tiny))
            continue;
        if (undo)
            undo->push(new UnregisterSymbolCommand(reg, it.key(), it.value()));
        else
            reg->unregisterSymbol(it.key());
    }
}

QString ExtrudeFeature::registerStandalone(MyGraphicsScene *scene, QUndoStack *undo, const QRectF &box,
                                           const QList<QPointF> &picks, const QPainterPath &region,
                                           const QString &name, double zStart, double depth, bool symmetric,
                                           QString *error)
{
    if (!scene || region.isEmpty()) {
        if (error)
            *error = QStringLiteral("돌출할 공간을 찾지 못했습니다.");
        return QString();
    }
    // 같은 자리에 다시 그린 경우: 지운 옛 판의 기록이 새 공간을 똑같이 찾아 겹쳐 보이므로 그 기록은 지운다 (undo 가능)
    removeSameStandalone(scene, undo, region);
    QMap<QString, QVariant> props;
    props[QStringLiteral("Name")] = name;
    props[QStringLiteral("Host")] = QString();
    props[QStringLiteral("Standalone")] = true;
    props[QStringLiteral("Box")] = QString::fromUtf8(
        QJsonDocument(QJsonArray{box.x(), box.y(), box.width(), box.height()}).toJson(QJsonDocument::Compact));
    QJsonArray pa;
    for (int i = 0; i < picks.size(); ++i)
        pa.append(QJsonArray{picks.at(i).x(), picks.at(i).y()});
    props[QStringLiteral("Picks")] = QString::fromUtf8(QJsonDocument(pa).toJson(QJsonDocument::Compact));
    props[QStringLiteral("Region")] = regionJson(region);
    props[QStringLiteral("Offset")] = zStart;
    props[QStringLiteral("Depth")] = depth;
    props[QStringLiteral("Symmetric")] = symmetric;
    props[QStringLiteral("Merge")] = false;
    const QPointF ref = region.boundingRect().topLeft();
    const QString uuid = MechPartTag::registerPart(scene, undo, nullptr, QStringLiteral("MechExtrude"), name, props,
                                                   ref, 0.0, &ref);
    if (uuid.isEmpty() && error)
        *error = QStringLiteral("부품 등록에 실패했습니다.");
    return uuid;
}

QList<QString> ExtrudeFeature::attachedTo(MyGraphicsScene *scene, const QString &hostUuid)
{
    QList<QString> out;
    if (!scene)
        return out;
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it)
        if (it.value().type == QLatin1String("MechExtrude") &&
            it.value().props.value(QStringLiteral("Host")).toString() == hostUuid)
            out << it.key();
    return out;
}

bool ExtrudeFeature::resolve(MyGraphicsScene *scene, const QString &uuid, ExtrudeInfo &info)
{
    if (!scene || !scene->symbolRegistry()->hasSymbol(uuid))
        return false;
    const SymbolInfo si = scene->symbolRegistry()->symbolInfo(uuid);
    const QMap<QString, QVariant> &pr = si.props;
    info.uuid = uuid;
    info.name = si.name;
    info.host = pr.value(QStringLiteral("Host")).toString();
    info.viewId = pr.value(QStringLiteral("ViewId")).toString();
    info.viewKind = pr.value(QStringLiteral("ViewKind")).toInt();
    info.offset = pr.value(QStringLiteral("Offset")).toDouble();
    info.depth = pr.value(QStringLiteral("Depth")).toDouble();
    info.merge = pr.value(QStringLiteral("Merge")).toBool();
    info.standalone = pr.value(QStringLiteral("Standalone")).toBool();
    info.symmetric = pr.value(QStringLiteral("Symmetric")).toBool();
    info.plateBoss = pr.value(QStringLiteral("PlateBoss")).toBool();
    info.plateFront = pr.value(QStringLiteral("PlateFront")).toBool();
    info.cut = pr.value(QStringLiteral("Cut")).toBool();
    info.through = pr.value(QStringLiteral("Through")).toBool();

    if (info.plateFront) {
        // 판 정면 파내기: 판 공간 안의 닫힌 모양 (판 자기 투상선은 경계에서 뺌) → 앞면 z1 에서 −Z
        const QJsonArray b = QJsonDocument::fromJson(pr.value(QStringLiteral("Box")).toString().toUtf8()).array();
        const QRectF box(b.at(0).toDouble(), b.at(1).toDouble(), b.at(2).toDouble(), b.at(3).toDouble());
        QList<QPointF> picks;
        const QJsonArray pj = QJsonDocument::fromJson(pr.value(QStringLiteral("Picks")).toString().toUtf8()).array();
        for (int i = 0; i < pj.size(); ++i)
            picks << QPointF(pj.at(i).toArray().at(0).toDouble(), pj.at(i).toArray().at(1).toDouble());
        const QPainterPath region =
            RevolveProfile::areaRegion(scene, box, picks, true, true, true, plateViewIds(scene, info.host));
        if (region.isEmpty())
            return false;
        ExtrudeInfo pe;
        QPointF pa0, pref;
        if (!PlateView::frame(scene, info.host, pe, pa0, pref))
            return false;
        info.standalone = true;                         // 도면 좌표 (X, Y) 그대로 (extrudePart 매핑)
        info.a0 = QPointF(0.0, 0.0);
        info.angleDeg = 0.0;
        info.rings = orientRings(cleanRings(region));
        info.x1 = pe.x1;
        info.x0 = info.through ? pe.x0 : std::max(pe.x0, pe.x1 - info.depth);
        return !info.rings.isEmpty() && info.x1 - info.x0 > 1e-6;
    }

    if (info.plateBoss) {
        // 판 돌기: 그린 뷰 안의 공간 (그 뷰의 판 투상선은 경계에서 뺌 — 돌기 뒤로 숨은선이 되며 공간을 쪼개지 않게)
        const QJsonArray b = QJsonDocument::fromJson(pr.value(QStringLiteral("Box")).toString().toUtf8()).array();
        const QRectF box(b.at(0).toDouble(), b.at(1).toDouble(), b.at(2).toDouble(), b.at(3).toDouble());
        QList<QPointF> picks;
        const QJsonArray pj = QJsonDocument::fromJson(pr.value(QStringLiteral("Picks")).toString().toUtf8()).array();
        for (int i = 0; i < pj.size(); ++i)
            picks << QPointF(pj.at(i).toArray().at(0).toDouble(), pj.at(i).toArray().at(1).toDouble());
        const QPainterPath region =
            RevolveProfile::areaRegion(scene, box, picks, true, true, true, QStringList{info.viewId});
        if (region.isEmpty())
            return false;                               // 공간 선을 지우면 돌기도 없어짐
        return PlateView::bossSection(scene, info.host, info.viewId, region, info.depth, info.merge, info);
    }

    if (info.standalone) {
        // 단독 돌출: 도면 공간 그대로 (X, Y), Z 범위 = 시작 Z ~ +깊이 (대칭이면 가운데 기준)
        info.merge = false;
        info.a0 = QPointF(0.0, 0.0);
        info.angleDeg = 0.0;
        if (info.symmetric) {
            info.x0 = info.offset - info.depth * 0.5;
            info.x1 = info.offset + info.depth * 0.5;
        } else {
            info.x0 = info.offset;
            info.x1 = info.offset + info.depth;
        }
        const QJsonArray b = QJsonDocument::fromJson(pr.value(QStringLiteral("Box")).toString().toUtf8()).array();
        const QRectF box(b.at(0).toDouble(), b.at(1).toDouble(), b.at(2).toDouble(), b.at(3).toDouble());
        QList<QPointF> picks;
        const QJsonArray pj = QJsonDocument::fromJson(pr.value(QStringLiteral("Picks")).toString().toUtf8()).array();
        for (int i = 0; i < pj.size(); ++i)
            picks << QPointF(pj.at(i).toArray().at(0).toDouble(), pj.at(i).toArray().at(1).toDouble());
        // 구멍R·구멍P(HoleItem)·구멍 단면 선은 경계가 아님 — 구멍 커터로 따로 판다 (그냥 원은 관통 구멍 섬)
        //   자기 투상도 선(정면 숨은선 등)도 빼서 판 공간이 쪼개지지 않게
        QStringList ownViews;
        const QJsonArray vj = QJsonDocument::fromJson(pr.value(QStringLiteral("Views")).toString().toUtf8()).array();
        for (int i = 0; i < vj.size(); ++i)
            ownViews << vj.at(i).toObject().value(QStringLiteral("id")).toString();
        //   고른 점 위에 나중에 그린 닫힌 모양이 있어도 등록 때 공간을 다시 찾도록 보정 (repickRegion)
        QPainterPath region =
            repickRegion(scene, box, picks, pr.value(QStringLiteral("Region")).toString(), ownViews, true);
        // 단독 돌출은 저장된 모양으로 대신하지 않는다: 경계선을 지우면 판도 없어진 것 (3D·투상도에서 빠짐, 선을 되살리면 다시 나타남)
        if (region.isEmpty())
            return false;
        info.rings = orientRings(cleanRings(region));
        // 정면 파내기 스케치(판 공간 안의 닫힌 모양)는 관통 구멍 섬이 아님 → 판 고리에서 뺌
        //   다른 단독 돌출이 된 섬(판 안쪽에 그려 따로 돌출한 모양)도 판의 관통 구멍이 아님 → 뺌
        QList<QRectF> cutBoxes = frontCutBoxes(scene, uuid, ownViews);
        cutBoxes << otherStandaloneBoxes(scene, uuid);
        if (!cutBoxes.isEmpty()) {
            QList<QPolygonF> kept, dropped;
            for (int r = 0; r < info.rings.size(); ++r) {
                bool drop = false;
                if (ringArea(info.rings.at(r)) < 0.0) {
                    const QRectF rb = info.rings.at(r).boundingRect();
                    for (int c = 0; c < cutBoxes.size() && !drop; ++c) {
                        const QRectF &cb = cutBoxes.at(c);
                        drop = std::fabs(rb.left() - cb.left()) < 1e-3 && std::fabs(rb.top() - cb.top()) < 1e-3 &&
                               std::fabs(rb.right() - cb.right()) < 1e-3 && std::fabs(rb.bottom() - cb.bottom()) < 1e-3;
                    }
                }
                if (!drop)
                    kept << info.rings.at(r);
                else
                    dropped << info.rings.at(r);
            }
            // 뺀 섬 안에 또 든 고리(예: 위 판에 그린 정면 파내기 스케치)도 이 판의 것이 아님 → 함께 뺌
            if (!dropped.isEmpty()) {
                QList<QPolygonF> kept2;
                for (int r = 0; r < kept.size(); ++r) {
                    const QPointF probe = (kept.at(r).at(0) + kept.at(r).at(1)) * 0.5;
                    bool inside = false;
                    for (int d = 0; d < dropped.size() && !inside; ++d)
                        inside = std::fabs(ringArea(dropped.at(d))) > std::fabs(ringArea(kept.at(r))) &&
                                 dropped.at(d).containsPoint(probe, Qt::OddEvenFill);
                    if (!inside)
                        kept2 << kept.at(r);
                }
                kept = kept2;
            }
            info.rings = kept;
        }
        return !info.rings.isEmpty() && info.x1 - info.x0 > 1e-6;
    }

    // 호스트 단면·축
    QList<RevolveLoop> loops;
    QString hostName;
    if (!MechModel3D::revolveSection(scene, info.host, loops, info.a0, info.angleDeg, hostName))
        return false;
    double xmin = 1e300, xmax = -1e300;
    for (int i = 0; i < loops.size(); ++i)
        for (int k = 0; k < loops.at(i).pts.size(); ++k) {
            xmin = std::min(xmin, loops.at(i).pts.at(k).x());
            xmax = std::max(xmax, loops.at(i).pts.at(k).x());
        }
    const bool left = (info.viewKind == ProjectionView::Left);
    if (left) {
        info.x0 = xmin + info.offset;
        info.x1 = info.x0 + info.depth;
    } else {
        info.x1 = xmax - info.offset;
        info.x0 = info.x1 - info.depth;
    }

    // 그린 측면도의 현재 중심
    const QList<ProjectionView::SideViewInfo> svs = ProjectionView::sideViews(scene, info.host);
    const ProjectionView::SideViewInfo *sv = nullptr;
    for (int i = 0; i < svs.size() && !sv; ++i)
        if (svs.at(i).viewId == info.viewId)
            sv = &svs.at(i);
    if (!sv)
        return false;

    // 공간 다시 찾기 (투상 원도 경계) — 측면도가 옮겨졌으면 그만큼 옮겨서 (hostExtrudeRegion)
    bool stale = false;
    const QPainterPath region = hostExtrudeRegion(scene, pr, info.viewId, sv->center, &stale);
    if (region.isEmpty())
        return false;
    // 측면도 중심을 모르는 옛 기록(ViewCx 없음)이 공간을 다시 못 찾으면 등록 때 절대 좌표 모양은 이미 엉뚱한 자리
    //   (측면도·스케치를 옮긴 뒤) → 어긋난 돌출을 그리지 않고 무시한다. 다시 돌출하면 새 기록으로 대체됨
    if (stale && !pr.contains(QStringLiteral("ViewCx")))
        return false;

    // 도면 → 로컬 (y, z)
    QList<QPolygonF> rings;
    const QList<QPolygonF> polys = region.toSubpathPolygons();
    for (int i = 0; i < polys.size(); ++i) {
        QPolygonF r;
        for (int k = 0; k < polys.at(i).size(); ++k) {
            const QPointF d = polys.at(i).at(k) - sv->center;
            const double du = d.x() * sv->u.x() + d.y() * sv->u.y();
            const double dn = d.x() * sv->n.x() + d.y() * sv->n.y();
            const QPointF q(dn, left ? du : -du);
            if (!r.isEmpty() && QLineF(r.last(), q).length() < 1e-6)
                continue;
            r << q;
        }
        while (r.size() > 1 && QLineF(r.first(), r.last()).length() < 1e-6)
            r.removeLast();
        if (r.size() >= 3 && std::fabs(ringArea(r)) > 1e-6)
            rings << r;
    }
    // 바깥 = 반시계, 안쪽 구멍 = 시계
    info.rings = orientRings(rings);
    return !info.rings.isEmpty() && info.x1 - info.x0 > 1e-6;
}

bool ExtrudeFeature::findHostView(MyGraphicsScene *scene, const QPainterPath &region, QString &host,
                                  QString &viewId, int &viewKind)
{
    if (!scene || region.isEmpty())
        return false;
    const QPointF c = region.boundingRect().center();
    const double size = std::max(region.boundingRect().width(), region.boundingRect().height());
    double best = 1e300;
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        if (it.value().type != QLatin1String("MechRevolve"))
            continue;
        const QList<ProjectionView::SideViewInfo> svs = ProjectionView::sideViews(scene, it.key());
        for (int i = 0; i < svs.size(); ++i) {
            const double d = QLineF(svs.at(i).center, c).length();
            if (d <= svs.at(i).rMax + size && d < best) {
                best = d;
                host = it.key();
                viewId = svs.at(i).viewId;
                viewKind = svs.at(i).kind;
            }
        }
    }
    return best < 1e300;
}

QString ExtrudeFeature::registerPlateBoss(MyGraphicsScene *scene, QUndoStack *undo, const QString &plate,
                                          const QString &viewId, int viewKind, const QRectF &box,
                                          const QList<QPointF> &picks, const QPainterPath &region, const QString &name,
                                          double depth, bool merge, QString *error, bool cut, bool through)
{
    if (!scene || region.isEmpty() || plate.isEmpty()) {
        if (error)
            *error = QStringLiteral("돌출할 공간 또는 판의 투상도를 찾지 못했습니다.");
        return QString();
    }
    QMap<QString, QVariant> props;
    props[QStringLiteral("Name")] = name;
    props[QStringLiteral("Host")] = plate;
    props[QStringLiteral("ViewId")] = viewId;
    props[QStringLiteral("ViewKind")] = viewKind;
    props[QStringLiteral("PlateBoss")] = true;
    props[QStringLiteral("Cut")] = cut;
    props[QStringLiteral("Through")] = through;
    props[QStringLiteral("Box")] = QString::fromUtf8(
        QJsonDocument(QJsonArray{box.x(), box.y(), box.width(), box.height()}).toJson(QJsonDocument::Compact));
    QJsonArray pa;
    for (int i = 0; i < picks.size(); ++i)
        pa.append(QJsonArray{picks.at(i).x(), picks.at(i).y()});
    props[QStringLiteral("Picks")] = QString::fromUtf8(QJsonDocument(pa).toJson(QJsonDocument::Compact));
    props[QStringLiteral("Region")] = regionJson(region);
    props[QStringLiteral("Offset")] = 0.0;
    props[QStringLiteral("Depth")] = depth;
    props[QStringLiteral("Merge")] = merge;
    const QPointF ref = region.boundingRect().topLeft();
    const QString uuid = MechPartTag::registerPart(scene, undo, nullptr, QStringLiteral("MechExtrude"), name, props,
                                                   ref, 0.0, &ref);
    if (uuid.isEmpty() && error)
        *error = QStringLiteral("부품 등록에 실패했습니다.");
    return uuid;
}

QString ExtrudeFeature::registerPlateFrontCut(MyGraphicsScene *scene, QUndoStack *undo, const QString &plate,
                                              const QRectF &box, const QList<QPointF> &picks,
                                              const QPainterPath &region, const QString &name, double depth,
                                              bool through, QString *error)
{
    if (!scene || region.isEmpty() || plate.isEmpty()) {
        if (error)
            *error = QStringLiteral("파낼 공간 또는 판을 찾지 못했습니다.");
        return QString();
    }
    QMap<QString, QVariant> props;
    props[QStringLiteral("Name")] = name;
    props[QStringLiteral("Host")] = plate;
    props[QStringLiteral("PlateFront")] = true;
    props[QStringLiteral("Cut")] = true;
    props[QStringLiteral("Through")] = through;
    props[QStringLiteral("Box")] = QString::fromUtf8(
        QJsonDocument(QJsonArray{box.x(), box.y(), box.width(), box.height()}).toJson(QJsonDocument::Compact));
    QJsonArray pa;
    for (int i = 0; i < picks.size(); ++i)
        pa.append(QJsonArray{picks.at(i).x(), picks.at(i).y()});
    props[QStringLiteral("Picks")] = QString::fromUtf8(QJsonDocument(pa).toJson(QJsonDocument::Compact));
    props[QStringLiteral("Region")] = regionJson(region);
    props[QStringLiteral("Offset")] = 0.0;
    props[QStringLiteral("Depth")] = depth;
    props[QStringLiteral("Merge")] = false;
    const QPointF ref = region.boundingRect().topLeft();
    const QString uuid = MechPartTag::registerPart(scene, undo, nullptr, QStringLiteral("MechExtrude"), name, props,
                                                   ref, 0.0, &ref);
    if (uuid.isEmpty() && error)
        *error = QStringLiteral("부품 등록에 실패했습니다.");
    return uuid;
}

QString ExtrudeFeature::registerExtrude(MyGraphicsScene *scene, QUndoStack *undo, const QString &host,
                                        const QString &viewId, int viewKind, const QRectF &box,
                                        const QList<QPointF> &picks, const QPainterPath &region,
                                        const QString &name, double offset, double depth, bool merge,
                                        QString *error, bool cut)
{
    if (!scene || region.isEmpty() || host.isEmpty()) {
        if (error)
            *error = QStringLiteral("돌출할 공간 또는 측면도를 찾지 못했습니다.");
        return QString();
    }
    // 같은 측면도의 같은 공간을 다시 돌출(파내기)한 경우: 옛 기록을 지우고 새 값으로 대체 (undo 가능)
    //   — 정면도 선만 지우고 다시 돌출하면 옛 기록이 남아 두 돌출이 겹쳐 그려지던 문제
    {
        SceneSymbolRegistry *reg = scene->symbolRegistry();
        const QMap<QString, SymbolInfo> all = reg->allSymbols();   // 복사 (도는 중에 지움)
        const double tiny = 1e-3;
        QPointF svc;
        const bool hasC = sideViewCenter(scene, host, viewId, svc);
        for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
            const QMap<QString, QVariant> &p = it.value().props;
            if (it.value().type != QLatin1String("MechExtrude") || p.value(QStringLiteral("Standalone")).toBool() ||
                p.value(QStringLiteral("PlateBoss")).toBool() || p.value(QStringLiteral("PlateFront")).toBool() ||
                p.value(QStringLiteral("Host")).toString() != host ||
                p.value(QStringLiteral("ViewKind")).toInt() != viewKind || p.value(QStringLiteral("Cut")).toBool() != cut)
                continue;
            // 옛 기록의 지금 공간 — 같은 방향(좌/우)이면 다른(숨긴·지운 뒤 다시 만든) 측면도 기록도 비교:
            //   옛 기록은 자기 측면도 중심 기준 위치를 새 측면도 중심 기준으로 옮겨 비교 (측면도가 옮겨졌으면 따라감, 못 찾으면 stale)
            const QString oldView = p.value(QStringLiteral("ViewId")).toString();
            QPointF oc;
            const bool hasOld = sideViewCenter(scene, host, oldView, oc);
            bool stale = false;
            QPainterPath old, oldAbs;
            if (hasC && hasOld) {
                oldAbs = hostExtrudeRegion(scene, p, oldView, oc, &stale);   // 도면 위 그 공간
                old = oldAbs.translated(svc - oc);                           // 새 측면도 기준으로 옮긴 것
            } else if (oldView == viewId) {
                old = regionFromJson(p.value(QStringLiteral("Region")).toString());
            }
            if (old.isEmpty())
                continue;
            QRectF d1 = old.subtracted(region).boundingRect();
            QRectF d2 = region.subtracted(old).boundingRect();
            bool same = !((d1.width() >= tiny && d1.height() >= tiny) || (d2.width() >= tiny && d2.height() >= tiny));
            if (!same && !stale && !oldAbs.isEmpty()) {
                // 다른 측면도에 묶인 옛 기록이 지금 도면의 같은 스케치(공간)를 찾고 있음 → 같은 돌출을 다시 한 것
                const QRectF nb = region.boundingRect();
                const double tolA = std::max(0.05, 0.005 * std::max(nb.width(), nb.height()));
                d1 = oldAbs.subtracted(region).boundingRect();
                d2 = region.subtracted(oldAbs).boundingRect();
                same = !((d1.width() >= tolA && d1.height() >= tolA) || (d2.width() >= tolA && d2.height() >= tolA));
            }
            if (!same && stale) {
                // 다시 못 찾는 옛 기록(스케치를 옮긴 뒤 고른 점이 빈 곳)이 같은 모양이면 옮겨서 다시 돌출한 것 → 대체
                //   원호가 섞인 윤곽은 다시 찾을 때마다 꼭짓점이 조금씩 달라 넉넉한 허용오차(크기의 0.5 %)로 비교
                const QRectF nb = region.boundingRect(), ob = old.boundingRect();
                const double tolM = std::max(0.05, 0.005 * std::max(nb.width(), nb.height()));
                if (std::fabs(nb.width() - ob.width()) < tolM && std::fabs(nb.height() - ob.height()) < tolM) {
                    const QPainterPath moved = old.translated(nb.center() - ob.center());
                    d1 = moved.subtracted(region).boundingRect();
                    d2 = region.subtracted(moved).boundingRect();
                    same = !((d1.width() >= tolM && d1.height() >= tolM) || (d2.width() >= tolM && d2.height() >= tolM));
                }
            }
            if (!same)
                continue;                               // 다른 공간
            if (undo)
                undo->push(new UnregisterSymbolCommand(reg, it.key(), it.value()));
            else
                reg->unregisterSymbol(it.key());
        }
    }
    QMap<QString, QVariant> props;
    props[QStringLiteral("Name")] = name;
    props[QStringLiteral("Host")] = host;
    props[QStringLiteral("ViewId")] = viewId;
    props[QStringLiteral("ViewKind")] = viewKind;
    props[QStringLiteral("Box")] = QString::fromUtf8(
        QJsonDocument(QJsonArray{box.x(), box.y(), box.width(), box.height()}).toJson(QJsonDocument::Compact));
    QJsonArray pa;
    for (int i = 0; i < picks.size(); ++i)
        pa.append(QJsonArray{picks.at(i).x(), picks.at(i).y()});
    props[QStringLiteral("Picks")] = QString::fromUtf8(QJsonDocument(pa).toJson(QJsonDocument::Compact));
    props[QStringLiteral("Region")] = regionJson(region);
    props[QStringLiteral("Offset")] = offset;
    props[QStringLiteral("Depth")] = depth;
    props[QStringLiteral("Merge")] = merge && !cut;
    props[QStringLiteral("Cut")] = cut;
    // 등록 때 측면도 중심 — 측면도(와 스케치)를 옮기면 고른 점·모양을 그만큼 옮겨 다시 찾는다
    {
        QPointF c;
        if (sideViewCenter(scene, host, viewId, c)) {
            props[QStringLiteral("ViewCx")] = c.x();
            props[QStringLiteral("ViewCy")] = c.y();
        }
    }
    const QPointF ref = region.boundingRect().topLeft();
    const QString uuid = MechPartTag::registerPart(scene, undo, nullptr, QStringLiteral("MechExtrude"), name, props,
                                                   ref, 0.0, &ref);
    if (uuid.isEmpty() && error)
        *error = QStringLiteral("부품 등록에 실패했습니다.");
    return uuid;
}
