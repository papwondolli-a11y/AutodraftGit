#include "extrudefeature.h"

#include "caditem.h"
#include "hatchitem.h"
#include "holedata.h"
#include "iappcontext.h"

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
    return RevolveProfile::areaRegion(scene, box, picks, true, plateFlags, plateFlags, excludeViewIds, true);   // 중심선 레이어 제외
}

double rectArea(const QRectF &r) { return std::fabs(r.width() * r.height()); }

// 두 공간이 얼마나 겹치나 = 겹친 넓이 / 합친 넓이 (0 ~ 1)
//   두 경로를 덮는 사각을 격자로 나눠 칸 가운데 점이 어느 쪽에 드는지 세어 어림한다 (구멍 고리도 그대로 반영)
double overlapRatio(const QPainterPath &a, const QPainterPath &b)
{
    if (a.isEmpty() || b.isEmpty())
        return 0.0;
    const QRectF bb = a.boundingRect().united(b.boundingRect());
    const int n = 48;
    const double cw = bb.width() / n, ch = bb.height() / n;
    if (cw <= 0.0 || ch <= 0.0)
        return 0.0;
    int both = 0, any = 0;
    for (int iy = 0; iy < n; ++iy)
        for (int ix = 0; ix < n; ++ix) {
            const QPointF p(bb.left() + (ix + 0.5) * cw, bb.top() + (iy + 0.5) * ch);
            const bool ina = a.contains(p), inb = b.contains(p);
            if (ina || inb)
                ++any;
            if (ina && inb)
                ++both;
        }
    return any > 0 ? double(both) / double(any) : 0.0;
}

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

// 가장 큰 고리 (바깥 윤곽)
QPolygonF outerRing(const QPainterPath &region)
{
    QPolygonF best;
    double a = -1.0;
    const QList<QPolygonF> polys = region.toSubpathPolygons();
    for (int i = 0; i < polys.size(); ++i)
        if (std::fabs(ringArea(polys.at(i))) > a) {
            a = std::fabs(ringArea(polys.at(i)));
            best = polys.at(i);
        }
    return best;
}

// 두 고리가 (거의) 같은 모양인가 — 크기의 0.5 % 까지
bool sameRing(const QPolygonF &a, const QPolygonF &b)
{
    if (a.size() < 3 || b.size() < 3)
        return false;
    const QRectF bb = b.boundingRect();
    const double tol = std::max(0.05, 0.005 * std::max(bb.width(), bb.height()));
    QPainterPath pa, pb;
    pa.addPolygon(a);
    pb.addPolygon(b);
    const QRectF d1 = pa.subtracted(pb).boundingRect(), d2 = pb.subtracted(pa).boundingRect();
    return !((d1.width() >= tol && d1.height() >= tol) || (d2.width() >= tol && d2.height() >= tol));
}

// 고리 둘레가 모두(97 %) 도면 선 위에 있는가 — 경계에서 빼는 뷰(excludeViewIds)의 선은 치지 않음
bool ringFullyDrawn(MyGraphicsScene *scene, const QPolygonF &ring, const QStringList &excludeViewIds)
{
    QPainterPath one;
    one.addPolygon(ring);
    one.closeSubpath();
    const QRectF bb = ring.boundingRect();
    const double tol = std::max(0.02, 2e-4 * std::max(bb.width(), bb.height()));
    const int n = 60;
    int hit = 0;
    for (int k = 0; k < n; ++k) {
        const QPointF q = one.pointAtPercent((k + 0.5) / n);
        const QRectF qr(q.x() - tol, q.y() - tol, 2.0 * tol, 2.0 * tol);
        const QList<QGraphicsItem *> near = scene->items(qr, Qt::IntersectsItemShape);
        for (int t = 0; t < near.size(); ++t) {
            const CadItem *ci = dynamic_cast<const CadItem *>(near.at(t));
            if (!ci || dynamic_cast<const HatchItem *>(ci) ||
                excludeViewIds.contains(ci->data(MECH_VIEW_ID).toString()))
                continue;
            ++hit;
            break;
        }
    }
    return hit * 100 >= n * 97;
}

// 공간 다시 찾기 + 고른 점 보정
//   저장된 고른 점(picks)으로 공간을 다시 찾되,
//   ⓪ 다른 모양의 선이 겹쳐 지나가면(판 두 장을 겹침) 공간이 조각나 일부만 잡힌다
//      → 바깥 윤곽이 등록 때와 다르지만 등록 윤곽 선이 고스란히 남아 있으면 등록 모양을 쓴다
//   ① 나중에 그 점 위에 다른 닫힌 모양(예: 판 안쪽에 그린 다각형)을 그리면 고른 점이 그 작은 모양 안에 들어가
//      "판 = 안쪽 다각형" 으로 바뀌어 버린다.
//   ② 모양을 지우거나 옮겨 고른 점이 빈 곳에 떨어지면 도면틀 안쪽 같은 엉뚱한 큰 공간이 잡힌다
//      (받침대·판이 도면틀만 하게 커지던 문제).
//   → 다시 찾은 공간이 등록 때 모양(Region)과 다르면 등록 모양 안의 점(①은 덮지 않는 부분)으로 다시 골라
//     등록 모양에 더 가까운 쪽을 쓰고, 그래도 등록 모양보다 4배 넘게 크면 그 모양은 없어진 것으로 본다(빈 경로).
//   strict (단독 돌출 = 판): 다시 찾은 공간이 등록 모양과 거의 안 겹치면(겹친 넓이/합친 넓이 < 0.25) 없어진 것으로 봄
//     — 판을 지운 뒤 그 자리에 그린 다른 부품(축 투상선 등) 사이의 작은 공간을 옛 판 기록이 잡아 되살아나던 문제
QPainterPath repickRegion(MyGraphicsScene *scene, const QRectF &box, const QList<QPointF> &picks,
                          const QString &storedJson, const QStringList &excludeViewIds, bool plateFlags,
                          bool strict = false)
{
    QPainterPath region = findArea(scene, box, picks, excludeViewIds, plateFlags);
    const QPainterPath stored = regionFromJson(storedJson);
    if (stored.isEmpty() || region.isEmpty())
        return region;
    const QRectF sb = stored.boundingRect();
    QRectF rb = region.boundingRect();
    const double tol = std::max(1e-3, 1e-4 * std::max(sb.width(), sb.height()));
    const QPolygonF storedOuter = outerRing(stored);
    if (!sameRing(outerRing(region), storedOuter) && ringFullyDrawn(scene, storedOuter, excludeViewIds))
        return stored;                                  // ⓪ 겹친 선에 조각남 — 등록 모양
    if (sameRect(sb, rb, tol))
        return region;                                  // 등록 때와 같은 바깥 경계 — 그대로
    // 고른 점 없는 옛 기록(복합모양 B, 지금은 없앤 옵션): 사각 안 아무 닫힌 선이나 잡히므로
    //   등록 모양과 절반 이상 겹칠 때만 같은 판으로 본다 (선을 조금 고친 것은 따라가고,
    //   지우고 같은 범위에 새로 그린 다른 모양은 옛 판이 되살아나지 않게)
    if (picks.isEmpty())
        return overlapRatio(stored, region) >= 0.5 ? region : QPainterPath();
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
    if (strict && overlapRatio(stored, region) < 0.25)
        return QPainterPath();                          // 등록 모양과 딴판인 공간 — 다른 선을 잘못 잡음
    return region;
}

// 부품 host 의 viewId 투상도가 숨겨져 있는가
bool viewConcealed(MyGraphicsScene *scene, const QString &host, const QString &viewId)
{
    if (!scene->symbolRegistry()->hasSymbol(host))
        return false;
    const QJsonArray vj = QJsonDocument::fromJson(
                              scene->symbolRegistry()->symbolInfo(host).props.value(QStringLiteral("Views")).toString().toUtf8())
                              .array();
    for (int i = 0; i < vj.size(); ++i)
        if (vj.at(i).toObject().value(QStringLiteral("id")).toString() == viewId)
            return vj.at(i).toObject().value(QStringLiteral("concealed")).toBool();
    return false;
}

// 합친 판의 스케치 선을 껐는가 (자기 또는 자기가 쓰인 A 의 "SketchHidden")
bool sketchOff(MyGraphicsScene *scene, const QMap<QString, QVariant> &p)
{
    if (p.value(QStringLiteral("SketchHidden")).toBool())
        return true;
    const QString into = p.value(QStringLiteral("BoolInto")).toString();
    return !into.isEmpty() && scene->symbolRegistry()->hasSymbol(into) &&
           scene->symbolRegistry()->symbolInfo(into).props.value(QStringLiteral("SketchHidden")).toBool();
}

// 판의 측면·보조 뷰에 그린 돌기·파내기의 현재 공간 (그 뷰의 판 투상선은 경계에서 뺌)
//   숨긴 뷰는 스케치 선도 숨으므로 기록 모양을 그대로 씀
QPainterPath bossRegion(MyGraphicsScene *scene, const QMap<QString, QVariant> &pr)
{
    const QString viewId = pr.value(QStringLiteral("ViewId")).toString();
    if (viewConcealed(scene, pr.value(QStringLiteral("Host")).toString(), viewId))
        return pr.value(QStringLiteral("ConcealedGone")).toBool() ? QPainterPath() : regionFromJson(pr.value(QStringLiteral("Region")).toString());
    const QJsonArray b = QJsonDocument::fromJson(pr.value(QStringLiteral("Box")).toString().toUtf8()).array();
    const QRectF box(b.at(0).toDouble(), b.at(1).toDouble(), b.at(2).toDouble(), b.at(3).toDouble());
    QList<QPointF> picks;
    const QJsonArray pj = QJsonDocument::fromJson(pr.value(QStringLiteral("Picks")).toString().toUtf8()).array();
    for (int i = 0; i < pj.size(); ++i)
        picks << QPointF(pj.at(i).toArray().at(0).toDouble(), pj.at(i).toArray().at(1).toDouble());
    // 스케치를 옮겨 고른 점이 빈 곳이 되면 도면틀 같은 엉뚱한 큰 공간이 잡히므로 등록 모양과 비교 (repickRegion)
    return repickRegion(scene, box, picks, pr.value(QStringLiteral("Region")).toString(), QStringList{viewId}, true);
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
    // 숨긴 측면도: 투상 원·스케치 선이 없으므로 기록 모양을 그대로 (숨길 때 freezeRegions 로 맞춰 둠)
    if (viewConcealed(scene, pr.value(QStringLiteral("Host")).toString(), viewId)) {
        if (stale)
            *stale = false;
        return pr.value(QStringLiteral("ConcealedGone")).toBool() ? QPainterPath() : stored;
    }
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

// 이 모양의 둘레를 따라 도면 선(사용자 선·그린 측면도의 투상 선)이 아직 있는가 — 둘레 표본의 60 % 이상
//   꺼진 레이어의 선도 있는 것으로 본다 (레이어를 껐다고 3D 부품이 사라지지 않게)
bool outlineStillDrawn(MyGraphicsScene *scene, const QPainterPath &region, const QString &viewId)
{
    const QList<QPolygonF> polys = region.toSubpathPolygons();
    const QList<QGraphicsItem *> items = scene->items();
    int total = 0, hit = 0;
    for (int i = 0; i < polys.size(); ++i) {
        QPainterPath one;
        one.addPolygon(polys.at(i));
        const int n = 40;
        for (int k = 0; k < n; ++k) {
            const QPointF q = one.pointAtPercent((k + 0.5) / n);
            const QRectF qr(q.x() - 0.1, q.y() - 0.1, 0.2, 0.2);
            ++total;
            for (int t = 0; t < items.size(); ++t) {
                const CadItem *ci = dynamic_cast<const CadItem *>(items.at(t));
                if (!ci || dynamic_cast<const HatchItem *>(ci))
                    continue;
                const QString v = ci->data(MECH_VIEW_ID).toString();
                if (!v.isEmpty() && v != viewId)
                    continue;                           // 다른 투상도의 선
                if (!ci->sceneBoundingRect().adjusted(-0.2, -0.2, 0.2, 0.2).intersects(qr))
                    continue;
                if (ci->mapToScene(ci->shape()).intersects(qr)) {
                    ++hit;
                    break;
                }
            }
        }
    }
    return total > 0 && hit * 10 >= total * 6;
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
            repickRegion(scene, box, picks, p.value(QStringLiteral("Region")).toString(), views, true, true);
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
        const QPainterPath region = RevolveProfile::areaRegion(scene, box, picks, true, true, true, ownViews, true);
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

QPainterPath ExtrudeFeature::currentArea(MyGraphicsScene *scene, const QString &uuid)
{
    if (!scene || !scene->symbolRegistry()->hasSymbol(uuid))
        return QPainterPath();
    const QMap<QString, QVariant> pr = scene->symbolRegistry()->symbolInfo(uuid).props;
    const QString host = pr.value(QStringLiteral("Host")).toString();
    const QString viewId = pr.value(QStringLiteral("ViewId")).toString();
    const bool plateBoss = pr.value(QStringLiteral("PlateBoss")).toBool();
    const bool plateFront = pr.value(QStringLiteral("PlateFront")).toBool();
    const bool standalone = pr.value(QStringLiteral("Standalone")).toBool();
    if (!plateBoss && !plateFront && !standalone) {     // 회전체 측면도
        QPointF c;
        if (!sideViewCenter(scene, host, viewId, c))
            return QPainterPath();
        return hostExtrudeRegion(scene, pr, viewId, c, nullptr);
    }
    const QJsonArray b = QJsonDocument::fromJson(pr.value(QStringLiteral("Box")).toString().toUtf8()).array();
    const QRectF box(b.at(0).toDouble(), b.at(1).toDouble(), b.at(2).toDouble(), b.at(3).toDouble());
    QList<QPointF> picks;
    const QJsonArray pj = QJsonDocument::fromJson(pr.value(QStringLiteral("Picks")).toString().toUtf8()).array();
    for (int i = 0; i < pj.size(); ++i)
        picks << QPointF(pj.at(i).toArray().at(0).toDouble(), pj.at(i).toArray().at(1).toDouble());
    if (plateBoss)
        return bossRegion(scene, pr);
    if (standalone && sketchOff(scene, pr))
        return regionFromJson(pr.value(QStringLiteral("Region")).toString());   // 스케치 선을 끈 합친 판
    // 경계로 치지 않는 뷰 (resolve 와 같은 규칙)
    const QStringList own = plateViewIds(scene, plateFront ? host : uuid);
    return repickRegion(scene, box, picks, pr.value(QStringLiteral("Region")).toString(), own, true,
                        standalone && !plateFront);     // 판은 엄격하게 (resolve 와 같은 규칙)
}

void ExtrudeFeature::freezeRegions(MyGraphicsScene *scene, QUndoStack *undo, const QString &host, const QString &viewId)
{
    if (!scene)
        return;
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    const QList<QString> ids = attachedTo(scene, host);
    for (int i = 0; i < ids.size(); ++i) {
        const SymbolInfo oldInfo = reg->symbolInfo(ids.at(i));
        ExtrudeInfo ei;
        if (oldInfo.props.value(QStringLiteral("ViewId")).toString() != viewId)
            continue;
        // 스케치를 지워 지금 없는 것은 숨긴 동안에도 없는 것으로 표시 (기록 모양으로 되살아나지 않게)
        const bool gone = !resolve(scene, ids.at(i), ei);
        const QPainterPath region = gone ? QPainterPath() : currentArea(scene, ids.at(i));
        SymbolInfo info = oldInfo;
        if (gone)
            info.props[QStringLiteral("ConcealedGone")] = true;
        else
            info.props.remove(QStringLiteral("ConcealedGone"));
        if (!region.isEmpty())
            info.props[QStringLiteral("Region")] = regionJson(region);
        // 측면도가 옮겨졌으면 사각·고른 점·등록 중심도 지금 자리로
        QPointF c;
        if (!gone && info.props.contains(QStringLiteral("ViewCx")) && sideViewCenter(scene, host, viewId, c)) {
            const QPointF d = c - QPointF(info.props.value(QStringLiteral("ViewCx")).toDouble(),
                                          info.props.value(QStringLiteral("ViewCy")).toDouble());
            const QJsonArray b = QJsonDocument::fromJson(info.props.value(QStringLiteral("Box")).toString().toUtf8()).array();
            info.props[QStringLiteral("Box")] = QString::fromUtf8(
                QJsonDocument(QJsonArray{b.at(0).toDouble() + d.x(), b.at(1).toDouble() + d.y(), b.at(2).toDouble(),
                                         b.at(3).toDouble()})
                    .toJson(QJsonDocument::Compact));
            const QJsonArray pj = QJsonDocument::fromJson(info.props.value(QStringLiteral("Picks")).toString().toUtf8()).array();
            QJsonArray np;
            for (int k = 0; k < pj.size(); ++k)
                np.append(QJsonArray{pj.at(k).toArray().at(0).toDouble() + d.x(), pj.at(k).toArray().at(1).toDouble() + d.y()});
            info.props[QStringLiteral("Picks")] = QString::fromUtf8(QJsonDocument(np).toJson(QJsonDocument::Compact));
            info.props[QStringLiteral("ViewCx")] = c.x();
            info.props[QStringLiteral("ViewCy")] = c.y();
        }
        if (undo)
            undo->push(new UpdateRegistryCommand(reg, ids.at(i), oldInfo, info));
        else
            reg->registerSymbol(ids.at(i), info);
    }
}

namespace {
// 도형 ci 가 공간 윤곽 polys 위에 놓인 선인가 (표본의 절반 이상)
bool liesOnOutline(const CadItem *ci, const QList<QPolygonF> &polys, double tol)
{
    const QPainterPath path = ci->sceneTransform().map(ci->geometryPath());
    if (path.isEmpty() || path.length() < 1e-9)
        return false;
    const int n = 20;
    int hit = 0;
    for (int s = 0; s < n; ++s) {
        const QPointF q = path.pointAtPercent((s + 0.5) / n);
        bool on = false;
        for (int p = 0; p < polys.size() && !on; ++p)
            for (int k = 0; k + 1 < polys.at(p).size() && !on; ++k) {
                const QPointF a = polys.at(p).at(k), d = polys.at(p).at(k + 1) - a;
                const double l2 = d.x() * d.x() + d.y() * d.y();
                const double t = l2 < 1e-12 ? 0.0 : qBound(0.0, ((q - a).x() * d.x() + (q - a).y() * d.y()) / l2, 1.0);
                on = QLineF(q, a + d * t).length() <= tol;
            }
        hit += on ? 1 : 0;
    }
    return hit * 2 >= n;
}

// 윤곽 위에 놓인 사용자 선(투상도 선·해칭·구멍·중심선 제외)을 out 에 모음
void collectSketch(const QList<QGraphicsItem *> &items, const QPainterPath &region, QSet<QGraphicsItem *> &out)
{
    if (region.isEmpty())
        return;
    const QRectF rb = region.boundingRect();
    const double tol = std::max(0.05, 0.002 * std::max(rb.width(), rb.height()));
    const QRectF nearBox = rb.adjusted(-tol, -tol, tol, tol);
    QList<QPolygonF> polys = region.toSubpathPolygons();
    for (int i = 0; i < polys.size(); ++i)
        if (!polys.at(i).isEmpty() && !polys.at(i).isClosed())
            polys[i] << polys.at(i).first();
    for (int i = 0; i < items.size(); ++i) {
        const CadItem *ci = dynamic_cast<const CadItem *>(items.at(i));
        if (!ci || dynamic_cast<const HatchItem *>(ci) || !ci->data(MECH_VIEW_ID).toString().isEmpty() ||
            !ci->data(HOLE_DATA_GROUP_ID).toString().isEmpty() || !ci->data(HOLE_SECTION_GROUP_ID).toString().isEmpty())
            continue;
        if (ci->sceneBoundingRect().adjusted(-tol, -tol, tol, tol).intersects(nearBox) && liesOnOutline(ci, polys, tol))
            out.insert(items.at(i));
    }
}
} // namespace

namespace {
// 기록의 사각·고른 점·모양을 d 만큼 옮김
void shiftRecord(SymbolInfo &info, const QPointF &d)
{
    const QJsonArray b = QJsonDocument::fromJson(info.props.value(QStringLiteral("Box")).toString().toUtf8()).array();
    info.props[QStringLiteral("Box")] = QString::fromUtf8(
        QJsonDocument(QJsonArray{b.at(0).toDouble() + d.x(), b.at(1).toDouble() + d.y(), b.at(2).toDouble(), b.at(3).toDouble()})
            .toJson(QJsonDocument::Compact));
    const QJsonArray pj = QJsonDocument::fromJson(info.props.value(QStringLiteral("Picks")).toString().toUtf8()).array();
    QJsonArray np;
    for (int k = 0; k < pj.size(); ++k)
        np.append(QJsonArray{pj.at(k).toArray().at(0).toDouble() + d.x(), pj.at(k).toArray().at(1).toDouble() + d.y()});
    info.props[QStringLiteral("Picks")] = QString::fromUtf8(QJsonDocument(np).toJson(QJsonDocument::Compact));
    info.props[QStringLiteral("Region")] =
        regionJson(regionFromJson(info.props.value(QStringLiteral("Region")).toString()).translated(d));
}

// 가장 큰 고리만 (바깥 윤곽)
QPainterPath outerLoop(const QPainterPath &region)
{
    QPainterPath out;
    double best = -1.0;
    const QList<QPolygonF> polys = region.toSubpathPolygons();
    for (int i = 0; i < polys.size(); ++i)
        if (std::fabs(ringArea(polys.at(i))) > best) {
            best = std::fabs(ringArea(polys.at(i)));
            out = QPainterPath();
            out.addPolygon(polys.at(i));
        }
    return out;
}
} // namespace

QMap<QString, QPointF> ExtrudeFeature::sketchesMovedBy(MyGraphicsScene *scene, const QSet<QGraphicsItem *> &moved)
{
    QMap<QString, QPointF> out;
    if (!scene || moved.isEmpty())
        return out;
    const QList<QGraphicsItem *> items = scene->items();
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        const QMap<QString, QVariant> &p = it.value().props;
        ExtrudeInfo ei;
        if (it.value().type != QLatin1String("MechExtrude") || !resolve(scene, it.key(), ei))
            continue;
        const QString host = p.value(QStringLiteral("Host")).toString(), viewId = p.value(QStringLiteral("ViewId")).toString();
        if (viewConcealed(scene, host, viewId))
            continue;                                   // 숨긴 스케치는 선택되지 않음
        QSet<QGraphicsItem *> sketch;
        collectSketch(items, outerLoop(currentArea(scene, it.key())), sketch);
        if (sketch.isEmpty() || !moved.contains(sketch))
            continue;
        QPointF c;
        sideViewCenter(scene, host, viewId, c);
        out.insert(it.key(), c);
    }
    return out;
}

QSet<QGraphicsItem *> ExtrudeFeature::sketchItemsOf(MyGraphicsScene *scene, const QString &uuid)
{
    QSet<QGraphicsItem *> out;
    if (scene)
        collectSketch(scene->items(), currentArea(scene, uuid), out);
    return out;
}

QSet<QGraphicsItem *> ExtrudeFeature::sharedSketchLines(MyGraphicsScene *scene, const QSet<QGraphicsItem *> &moved)
{
    QSet<QGraphicsItem *> out;
    if (!scene || moved.isEmpty())
        return out;
    // 같이 쓰는 선 = 통째로 옮기는 판의 경계(whole) 이면서 남는 판의 경계(left)인 선
    //   (판 하나의 변 하나만 일부러 옮길 때는 복사본을 남기지 않음)
    QSet<QGraphicsItem *> whole, left;
    const QList<QGraphicsItem *> items = scene->items();
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        const QMap<QString, QVariant> &p = it.value().props;
        if (it.value().type != QLatin1String("MechExtrude"))
            continue;
        if (viewConcealed(scene, p.value(QStringLiteral("Host")).toString(), p.value(QStringLiteral("ViewId")).toString()))
            continue;                                   // 숨긴 스케치는 선택되지 않음
        if (p.value(QStringLiteral("Standalone")).toBool() && sketchOff(scene, p))
            continue;                                   // 스케치를 끈 판은 기록 모양을 씀 (선과 무관)
        QSet<QGraphicsItem *> sketch;
        collectSketch(items, outerLoop(currentArea(scene, it.key())), sketch);
        if (sketch.isEmpty())
            continue;
        if (moved.contains(sketch)) {
            whole.unite(sketch);                        // 이 판 스케치를 통째로 옮김 → 판이 따라감
            continue;
        }
        for (QSet<QGraphicsItem *>::const_iterator s = sketch.constBegin(); s != sketch.constEnd(); ++s)
            if (moved.contains(*s))
                left.insert(*s);                        // 남는 판의 경계 선인데 옮겨짐
    }
    for (QSet<QGraphicsItem *>::const_iterator s = left.constBegin(); s != left.constEnd(); ++s)
        if (whole.contains(*s))
            out.insert(*s);
    return out;
}

void ExtrudeFeature::outlinesMovedBy(MyGraphicsScene *scene, const QSet<QGraphicsItem *> &moved,
                                     QMap<QString, QPointF> &records, QSet<QGraphicsItem *> &extra)
{
    if (!scene || moved.isEmpty())
        return;
    const QList<QGraphicsItem *> items = scene->items();
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        if (it.value().type != QLatin1String("MechExtrude") ||
            !it.value().props.value(QStringLiteral("SketchHidden")).toBool())
            continue;
        // 결과 외형선 (BoolMenu::drawOutline 이 붙이는 태그 — booltool.cpp outlineTag 와 같은 규칙)
        const QString tag = QStringLiteral("boolout:") + it.key();
        bool any = false, allMoved = true;
        for (int i = 0; i < items.size() && allMoved; ++i) {
            if (items.at(i)->data(MECH_VIEW_ID).toString() != tag)
                continue;
            any = true;
            allMoved = moved.contains(items.at(i));
        }
        if (!any || !allMoved)
            continue;                                   // 외형선 일부만 옮김 → 판은 그대로
        // A 와 그 B 들 (스케치를 끈 동안은 기록 모양 Region 을 쓰므로 기록을 옮기면 3D·투상도가 따라감)
        QStringList group{it.key()};
        for (QMap<QString, SymbolInfo>::const_iterator jt = all.constBegin(); jt != all.constEnd(); ++jt)
            if (jt.value().type == QLatin1String("MechExtrude") &&
                jt.value().props.value(QStringLiteral("BoolInto")).toString() == it.key())
                group << jt.key();
        for (int g = 0; g < group.size(); ++g) {
            if (!records.contains(group.at(g)))
                records.insert(group.at(g), QPointF());  // 단독 판은 측면도 중심 없음
            // 숨긴 스케치 선 (기록 모양 위의 사용자 선)
            QSet<QGraphicsItem *> sk;
            collectSketch(items, currentArea(scene, group.at(g)), sk);
            for (QSet<QGraphicsItem *>::const_iterator s = sk.constBegin(); s != sk.constEnd(); ++s)
                if (!moved.contains(*s))
                    extra.insert(*s);
        }
    }
}

void ExtrudeFeature::followMove(IAppContext *ctx, MyGraphicsScene *scene, const QMap<QString, QPointF> &records,
                                const QPointF &delta)
{
    if (!ctx || !scene || records.isEmpty())
        return;
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    QUndoStack *undo = ctx->undoStack();
    QSet<QString> parts;
    for (QMap<QString, QPointF>::const_iterator it = records.constBegin(); it != records.constEnd(); ++it) {
        if (!reg->hasSymbol(it.key()))
            continue;
        const SymbolInfo oldInfo = reg->symbolInfo(it.key());
        const QString host = oldInfo.props.value(QStringLiteral("Host")).toString();
        QPointF d = delta, c;
        // 측면도가 함께 옮겨진 만큼은 등록 중심(ViewCx)으로 이미 따라감
        if (oldInfo.props.contains(QStringLiteral("ViewCx")) &&
            sideViewCenter(scene, host, oldInfo.props.value(QStringLiteral("ViewId")).toString(), c))
            d -= c - it.value();
        parts.insert(host.isEmpty() ? it.key() : host);
        if (std::hypot(d.x(), d.y()) < 1e-9)
            continue;
        SymbolInfo info = oldInfo;
        shiftRecord(info, d);
        if (undo)
            undo->push(new UpdateRegistryCommand(reg, it.key(), oldInfo, info));
        else
            reg->registerSymbol(it.key(), info);
    }
    for (QSet<QString>::const_iterator p = parts.constBegin(); p != parts.constEnd(); ++p)
        ProjectionView::regeneratePart(ctx, scene, *p);
}

QString ExtrudeFeature::plateOfItem(MyGraphicsScene *scene, QGraphicsItem *item)
{
    if (!scene || !item)
        return QString();
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    // ① 판 투상도 선: 그 판의 Views 목록에 뷰 ID 가 있음
    const QString vid = item->data(MECH_VIEW_ID).toString();
    if (!vid.isEmpty()) {
        for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
            if (it.value().type != QLatin1String("MechExtrude") ||
                !it.value().props.value(QStringLiteral("Standalone")).toBool())
                continue;
            const QJsonArray vj =
                QJsonDocument::fromJson(it.value().props.value(QStringLiteral("Views")).toString().toUtf8()).array();
            for (int i = 0; i < vj.size(); ++i)
                if (vj.at(i).toObject().value(QStringLiteral("id")).toString() == vid)
                    return it.key();
        }
        return QString();
    }
    // ② 판 경계(바깥 윤곽) 위의 스케치 선 — 이동(MOVE) 따라가기와 같은 판정(collectSketch)
    const QList<QGraphicsItem *> one{item};
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        if (it.value().type != QLatin1String("MechExtrude") || !it.value().props.value(QStringLiteral("Standalone")).toBool())
            continue;
        QSet<QGraphicsItem *> hit;
        collectSketch(one, outerLoop(currentArea(scene, it.key())), hit);
        if (!hit.isEmpty())
            return it.key();
    }
    return QString();
}

bool ExtrudeFeature::setPlateHeight(IAppContext *ctx, MyGraphicsScene *scene, const QString &uuid, double bottom,
                                    double thickness, QString *error)
{
    if (!scene || !scene->symbolRegistry()->hasSymbol(uuid)) {
        if (error)
            *error = QStringLiteral("판을 찾지 못했습니다.");
        return false;
    }
    if (thickness < 0.01) {
        if (error)
            *error = QStringLiteral("두께는 0.01 mm 이상이어야 합니다.");
        return false;
    }
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    const SymbolInfo oldInfo = reg->symbolInfo(uuid);
    if (!oldInfo.props.value(QStringLiteral("Standalone")).toBool()) {
        if (error)
            *error = QStringLiteral("판(단독 돌출)이 아닙니다.");
        return false;
    }
    ExtrudeInfo before;
    if (!resolve(scene, uuid, before)) {
        if (error)
            *error = QStringLiteral("판의 공간을 찾지 못했습니다 (경계 선 확인).");
        return false;
    }
    // 같은 부품(부품 합치기 묶음) 안에서 이 판과 Z 범위가 같은 판 = 한 장으로 합쳐진 판 → 높이·두께를 똑같이
    QStringList coplanar;
    {
        const QString into = oldInfo.props.value(QStringLiteral("BoolInto")).toString();
        const QString root = (!into.isEmpty() && reg->hasSymbol(into)) ? into : uuid;
        const QMap<QString, SymbolInfo> &all = reg->allSymbols();
        for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
            if (it.key() == uuid || it.value().type != QLatin1String("MechExtrude") ||
                !it.value().props.value(QStringLiteral("Standalone")).toBool())
                continue;
            const QString in2 = it.value().props.value(QStringLiteral("BoolInto")).toString();
            const QString r2 = (!in2.isEmpty() && reg->hasSymbol(in2)) ? in2 : it.key();
            ExtrudeInfo e2;
            if (r2 == root && resolve(scene, it.key(), e2) && std::fabs(e2.x0 - before.x0) < 1e-3 &&
                std::fabs(e2.x1 - before.x1) < 1e-3)
                coplanar << it.key();
        }
    }
    // 위에 쌓인 판들: 바꾸기 전 모양으로 찾아 둔다 (윗면 변화량만큼 함께 옮김)
    const double delta = (bottom + thickness) - before.x1;
    QStringList stacked;
    if (std::fabs(delta) > 1e-9) {
        const QStringList seeds = QStringList{uuid} + coplanar;
        for (int s = 0; s < seeds.size(); ++s) {
            const QStringList up = platesStackedOn(scene, seeds.at(s));
            for (int k = 0; k < up.size(); ++k)
                if (!seeds.contains(up.at(k)) && !stacked.contains(up.at(k)))
                    stacked << up.at(k);
        }
    }

    QUndoStack *undo = ctx ? ctx->undoStack() : nullptr;
    if (undo)
        undo->beginMacro(QStringLiteral("판 높이"));
    SymbolInfo info = oldInfo;
    info.props[QStringLiteral("Offset")] = bottom;      // 시작 높이 = 밑면
    info.props[QStringLiteral("Depth")] = thickness;
    info.props[QStringLiteral("Symmetric")] = false;    // 가운데 대칭 기록은 밑면 기준으로 바꿔 저장
    if (undo)
        undo->push(new UpdateRegistryCommand(reg, uuid, oldInfo, info));
    else
        reg->registerSymbol(uuid, info);
    // 한 장으로 합쳐진 판: 높이·두께를 똑같이
    for (int i = 0; i < coplanar.size(); ++i) {
        const SymbolInfo co = reg->symbolInfo(coplanar.at(i));
        SymbolInfo cn = co;
        cn.props[QStringLiteral("Offset")] = bottom;
        cn.props[QStringLiteral("Depth")] = thickness;
        cn.props[QStringLiteral("Symmetric")] = false;
        if (undo)
            undo->push(new UpdateRegistryCommand(reg, coplanar.at(i), co, cn));
        else
            reg->registerSymbol(coplanar.at(i), cn);
    }
    // 위에 쌓인 판: 시작 높이(Offset)만 delta 만큼 (두께·대칭 여부 그대로 — 대칭이면 가운데가 옮겨져 같은 결과)
    for (int i = 0; i < stacked.size(); ++i) {
        const SymbolInfo so = reg->symbolInfo(stacked.at(i));
        SymbolInfo sn = so;
        sn.props[QStringLiteral("Offset")] = so.props.value(QStringLiteral("Offset")).toDouble() + delta;
        if (undo)
            undo->push(new UpdateRegistryCommand(reg, stacked.at(i), so, sn));
        else
            reg->registerSymbol(stacked.at(i), sn);
    }
    // 판들의 투상도·붙은 돌기·파내기 다시 만들기
    ProjectionView::regeneratePart(ctx, scene, uuid);
    for (int i = 0; i < coplanar.size(); ++i)
        ProjectionView::regeneratePart(ctx, scene, coplanar.at(i));
    for (int i = 0; i < stacked.size(); ++i)
        ProjectionView::regeneratePart(ctx, scene, stacked.at(i));
    if (undo)
        undo->endMacro();
    return true;
}

QStringList ExtrudeFeature::platesStackedOn(MyGraphicsScene *scene, const QString &uuid)
{
    QStringList out;
    if (!scene)
        return out;
    // 판 전부의 지금 모양·Z 범위
    QMap<QString, ExtrudeInfo> plates;
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        if (it.value().type != QLatin1String("MechExtrude") || !it.value().props.value(QStringLiteral("Standalone")).toBool())
            continue;
        ExtrudeInfo ei;
        if (resolve(scene, it.key(), ei) && !ei.rings.isEmpty())
            plates.insert(it.key(), ei);
    }
    if (!plates.contains(uuid))
        return out;
    // 부품 합치기(bool3d)로 묶인 같은 부품끼리만 따라감 — 개별 부품은 각자 높이를 가짐
    //   묶음의 기준 = A(남는 판): B 는 props "BoolInto" = A, A 는 자기 자신
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    QMap<QString, QString> rootOf;
    for (QMap<QString, ExtrudeInfo>::const_iterator it = plates.constBegin(); it != plates.constEnd(); ++it) {
        const QString into = reg->symbolInfo(it.key()).props.value(QStringLiteral("BoolInto")).toString();
        rootOf.insert(it.key(), (!into.isEmpty() && reg->hasSymbol(into)) ? into : it.key());
    }
    const QString root = rootOf.value(uuid);
    // 아래 판 윗면 = 위 판 밑면 이고 평면 모양이 겹치면 "쌓임". 위 판 위에 또 쌓인 판도 차례로 (넓이 우선)
    const double zTol = 1e-3;
    QStringList queue{uuid};
    QSet<QString> seen{uuid};
    while (!queue.isEmpty()) {
        const QString cur = queue.takeFirst();
        const ExtrudeInfo &base = plates[cur];
        QPainterPath basePath = ringsPath(base.rings);
        const QRectF bb = basePath.boundingRect();
        const double aTol = std::max(1e-3, 1e-4 * std::max(bb.width(), bb.height()));
        for (QMap<QString, ExtrudeInfo>::const_iterator it = plates.constBegin(); it != plates.constEnd(); ++it) {
            if (seen.contains(it.key()) || rootOf.value(it.key()) != root || std::fabs(it.value().x0 - base.x1) > zTol)
                continue;                               // 이미 고름 · 다른 부품 · 밑면이 윗면에 안 붙음
            const QRectF ov = basePath.intersected(ringsPath(it.value().rings)).boundingRect();
            if (ov.width() < aTol || ov.height() < aTol)
                continue;                               // 모서리만 닿거나 떨어져 있음
            seen.insert(it.key());
            out << it.key();
            queue << it.key();
        }
    }
    return out;
}

QSet<QGraphicsItem *> ExtrudeFeature::concealedSketchItems(MyGraphicsScene *scene)
{
    QSet<QGraphicsItem *> hide, keep;
    if (!scene)
        return hide;
    const QList<QGraphicsItem *> items = scene->items();
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        const QMap<QString, QVariant> &p = it.value().props;
        if (it.value().type != QLatin1String("MechExtrude") ||
            !viewConcealed(scene, p.value(QStringLiteral("Host")).toString(), p.value(QStringLiteral("ViewId")).toString()))
            continue;
        collectSketch(items, currentArea(scene, it.key()), hide);
    }
    // 스케치 선을 끈 합친 판 (A 와 그 B 들)
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        const QMap<QString, QVariant> &p = it.value().props;
        if (it.value().type == QLatin1String("MechExtrude") && p.value(QStringLiteral("Standalone")).toBool() &&
            sketchOff(scene, p))
            collectSketch(items, currentArea(scene, it.key()), hide);
    }
    if (hide.isEmpty())
        return hide;
    // 판의 윤곽·정면 파내기에도 쓰는 선은 숨기지 않음 (숨기면 그 판이 없어짐)
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        const QMap<QString, QVariant> &p = it.value().props;
        if (it.value().type == QLatin1String("MechExtrude") &&
            (p.value(QStringLiteral("Standalone")).toBool() || p.value(QStringLiteral("PlateFront")).toBool()) &&
            !sketchOff(scene, p))
            collectSketch(items, currentArea(scene, it.key()), keep);
    }
    hide.subtract(keep);
    return hide;
}

namespace {
// 단독 돌출 기록의 Z 범위 (resolve 와 같은 규칙)
void zRangeOf(const QMap<QString, QVariant> &p, double &z0, double &z1)
{
    const double off = p.value(QStringLiteral("Offset")).toDouble(), d = p.value(QStringLiteral("Depth")).toDouble();
    if (p.value(QStringLiteral("Symmetric")).toBool()) {
        z0 = off - d * 0.5;
        z1 = off + d * 0.5;
    } else {
        z0 = off;
        z1 = off + d;
    }
}

int opFromKey(const QString &k)
{
    if (k == QLatin1String("subtract"))
        return ExtrudeFeature::BoolSubtract;
    if (k == QLatin1String("intersect"))
        return ExtrudeFeature::BoolIntersect;
    return ExtrudeFeature::BoolUnion;
}

QJsonArray boolEntries(const QMap<QString, QVariant> &p)
{
    return QJsonDocument::fromJson(p.value(QStringLiteral("Bool")).toString().toUtf8()).array();
}

// A·B Z 범위와 방법 → B 의 역할
int roleFor(int op, double a0, double a1, double b0, double b1)
{
    const double e = 1e-6;
    const bool sameZ = std::fabs(a0 - b0) < e && std::fabs(a1 - b1) < e;
    if (op == ExtrudeFeature::BoolIntersect)
        return ExtrudeInfo::BoolConsumed;
    if (op == ExtrudeFeature::BoolUnion)
        return sameZ ? ExtrudeInfo::BoolConsumed : ExtrudeInfo::BoolMerged;
    if (b0 <= a0 + e && b1 >= a1 - e)
        return ExtrudeInfo::BoolConsumed;              // B 가 A 의 두께를 덮음 → 평면 빼기
    if (b1 <= a0 + e || b0 >= a1 - e)
        return ExtrudeInfo::BoolConsumed;              // Z 가 안 겹침 → 뺄 것 없음 (도구만 숨김)
    return ExtrudeInfo::BoolCutter;
}

// 평면 불리언 결과 → 고리 (바깥 반시계·구멍 시계)
QList<QPolygonF> pathRings(const QPainterPath &p)
{
    QPainterPath q = p.simplified();
    return orientRings(cleanRings(q));
}

int g_boolDepth = 0;                                    // 합치기 사슬 되부름 방어
} // namespace

QString ExtrudeFeature::boolOpKey(int op)
{
    return op == BoolSubtract ? QStringLiteral("subtract") : (op == BoolIntersect ? QStringLiteral("intersect")
                                                                                   : QStringLiteral("union"));
}

QPainterPath ExtrudeFeature::ringsPath(const QList<QPolygonF> &rings)
{
    QPainterPath p;
    p.setFillRule(Qt::OddEvenFill);
    for (int i = 0; i < rings.size(); ++i) {
        p.addPolygon(rings.at(i));
        p.closeSubpath();
    }
    return p;
}

bool ExtrudeFeature::previewBool(MyGraphicsScene *scene, const QString &a, const QString &b, int op,
                                 QPainterPath &result, double &z0, double &z1, bool &exact)
{
    ExtrudeInfo ea, eb;
    if (!scene || !resolve(scene, a, ea) || !resolve(scene, b, eb) || !ea.standalone || !eb.standalone)
        return false;
    const QPainterPath pa = ringsPath(ea.rings), pb = ringsPath(eb.rings);
    z0 = ea.x0;
    z1 = ea.x1;
    const int role = roleFor(op, ea.x0, ea.x1, eb.x0, eb.x1);
    exact = role == ExtrudeInfo::BoolConsumed;
    if (op == BoolIntersect) {
        result = pa.intersected(pb);
        z0 = std::max(ea.x0, eb.x0);
        z1 = std::min(ea.x1, eb.x1);
    } else if (op == BoolUnion) {
        result = pa.united(pb);
    } else {
        const bool zOverlap = eb.x1 > ea.x0 + 1e-6 && eb.x0 < ea.x1 - 1e-6;
        result = zOverlap ? pa.subtracted(pb) : pa;
    }
    return true;
}

bool ExtrudeFeature::setBool(MyGraphicsScene *scene, QUndoStack *undo, const QString &a, const QString &b, int op,
                             const QString &newName, QString *error)
{
    SceneSymbolRegistry *reg = scene ? scene->symbolRegistry() : nullptr;
    if (!reg || a == b || !reg->hasSymbol(a) || !reg->hasSymbol(b)) {
        if (error)
            *error = QStringLiteral("판 두 개를 골라야 합니다.");
        return false;
    }
    const SymbolInfo oa = reg->symbolInfo(a), ob = reg->symbolInfo(b);
    if (!ob.props.value(QStringLiteral("BoolInto")).toString().isEmpty() || !boolEntries(ob.props).isEmpty()) {
        if (error)
            *error = QStringLiteral("B 는 이미 다른 합치기에 쓰였거나 합친 판입니다. 먼저 풀기를 하세요.");
        return false;
    }
    if (!oa.props.value(QStringLiteral("BoolInto")).toString().isEmpty()) {
        if (error)
            *error = QStringLiteral("A 는 이미 다른 판의 도구로 쓰였습니다. 먼저 풀기를 하세요.");
        return false;
    }
    SymbolInfo na = oa, nb = ob;
    QJsonArray entries = boolEntries(oa.props);
    QJsonObject e;
    e.insert(QStringLiteral("with"), b);
    e.insert(QStringLiteral("op"), boolOpKey(op));
    entries.append(e);
    na.props[QStringLiteral("Bool")] = QString::fromUtf8(QJsonDocument(entries).toJson(QJsonDocument::Compact));
    if (!newName.trimmed().isEmpty() && newName.trimmed() != oa.name) {
        na.name = newName.trimmed();
        na.props[QStringLiteral("Name")] = newName.trimmed();
    }
    nb.props[QStringLiteral("BoolInto")] = a;
    if (undo) {
        undo->push(new UpdateRegistryCommand(reg, a, oa, na));
        undo->push(new UpdateRegistryCommand(reg, b, ob, nb));
    } else {
        reg->registerSymbol(a, na);
        reg->registerSymbol(b, nb);
    }
    return true;
}

QStringList ExtrudeFeature::clearBool(MyGraphicsScene *scene, QUndoStack *undo, const QString &uuid)
{
    QStringList touched;
    SceneSymbolRegistry *reg = scene ? scene->symbolRegistry() : nullptr;
    if (!reg || !reg->hasSymbol(uuid))
        return touched;
    QString a = uuid;
    QStringList onlyB;                                  // B 를 골랐으면 그 관계만
    const QString into = reg->symbolInfo(uuid).props.value(QStringLiteral("BoolInto")).toString();
    if (!into.isEmpty() && reg->hasSymbol(into)) {
        a = into;
        onlyB << uuid;
    }
    const SymbolInfo oa = reg->symbolInfo(a);
    const QJsonArray entries = boolEntries(oa.props);
    QJsonArray kept;
    QStringList freed;
    for (int i = 0; i < entries.size(); ++i) {
        const QString w = entries.at(i).toObject().value(QStringLiteral("with")).toString();
        if (onlyB.isEmpty() || onlyB.contains(w))
            freed << w;
        else
            kept.append(entries.at(i));
    }
    if (!onlyB.isEmpty() && !freed.contains(onlyB.first()))
        freed << onlyB.first();                         // 짝이 끊긴 B 도 풀어 줌
    if (freed.isEmpty())
        return touched;
    SymbolInfo na = oa;
    if (kept.isEmpty()) {
        na.props.remove(QStringLiteral("Bool"));
        na.props.remove(QStringLiteral("SketchHidden"));   // 합친 것이 없으면 스케치 선도 다시 보임
    }
    else
        na.props[QStringLiteral("Bool")] = QString::fromUtf8(QJsonDocument(kept).toJson(QJsonDocument::Compact));
    if (undo)
        undo->push(new UpdateRegistryCommand(reg, a, oa, na));
    else
        reg->registerSymbol(a, na);
    touched << a;
    for (int i = 0; i < freed.size(); ++i) {
        if (!reg->hasSymbol(freed.at(i)))
            continue;
        const SymbolInfo ob = reg->symbolInfo(freed.at(i));
        SymbolInfo nb = ob;
        nb.props.remove(QStringLiteral("BoolInto"));
        if (undo)
            undo->push(new UpdateRegistryCommand(reg, freed.at(i), ob, nb));
        else
            reg->registerSymbol(freed.at(i), nb);
        touched << freed.at(i);
    }
    return touched;
}

bool ExtrudeFeature::sketchHidden(MyGraphicsScene *scene, const QString &uuid)
{
    return scene && scene->symbolRegistry()->hasSymbol(uuid) &&
           sketchOff(scene, scene->symbolRegistry()->symbolInfo(uuid).props);
}

void ExtrudeFeature::setSketchHidden(MyGraphicsScene *scene, QUndoStack *undo, const QString &a, bool hidden)
{
    SceneSymbolRegistry *reg = scene ? scene->symbolRegistry() : nullptr;
    if (!reg || !reg->hasSymbol(a))
        return;
    // 끄기 전에 A·B 의 지금 모양을 기록 (선이 숨으면 다시 찾을 수 없음)
    QStringList ids;
    ids << a;
    const QJsonArray entries = boolEntries(reg->symbolInfo(a).props);
    for (int i = 0; i < entries.size(); ++i)
        ids << entries.at(i).toObject().value(QStringLiteral("with")).toString();
    for (int i = 0; i < ids.size(); ++i) {
        if (!reg->hasSymbol(ids.at(i)))
            continue;
        const SymbolInfo oldInfo = reg->symbolInfo(ids.at(i));
        SymbolInfo info = oldInfo;
        if (i == 0) {
            if (hidden)
                info.props[QStringLiteral("SketchHidden")] = true;
            else
                info.props.remove(QStringLiteral("SketchHidden"));
        }
        if (hidden) {
            const QPainterPath region = currentArea(scene, ids.at(i));
            if (!region.isEmpty())
                info.props[QStringLiteral("Region")] = regionJson(region);
        }
        if (undo)
            undo->push(new UpdateRegistryCommand(reg, ids.at(i), oldInfo, info));
        else
            reg->registerSymbol(ids.at(i), info);
    }
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
            RevolveProfile::areaRegion(scene, box, picks, true, true, true, plateViewIds(scene, info.host), true);
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
        const QPainterPath region = bossRegion(scene, pr);
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
        QPainterPath region = sketchOff(scene, pr)
                                  ? regionFromJson(pr.value(QStringLiteral("Region")).toString())   // 스케치 선을 끔
                                  : repickRegion(scene, box, picks, pr.value(QStringLiteral("Region")).toString(),
                                                 ownViews, true, true);
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
        // 부품 합치기: 이 판이 B(도구)면 역할, A 면 B 들을 평면 모양에 계산
        const QString into = pr.value(QStringLiteral("BoolInto")).toString();
        if (!into.isEmpty() && scene->symbolRegistry()->hasSymbol(into)) {
            const QMap<QString, QVariant> pa = scene->symbolRegistry()->symbolInfo(into).props;
            const QJsonArray ae = boolEntries(pa);
            for (int i = 0; i < ae.size(); ++i)
                if (ae.at(i).toObject().value(QStringLiteral("with")).toString() == uuid) {
                    double a0 = 0.0, a1 = 0.0;
                    zRangeOf(pa, a0, a1);
                    info.boolRole = roleFor(opFromKey(ae.at(i).toObject().value(QStringLiteral("op")).toString()), a0, a1,
                                            info.x0, info.x1);
                    info.boolTarget = into;
                }
        }
        const QJsonArray entries = boolEntries(pr);
        if (!entries.isEmpty() && g_boolDepth < 4) {
            ++g_boolDepth;
            QPainterPath shape = ringsPath(info.rings);
            bool changed = false;
            for (int i = 0; i < entries.size(); ++i) {
                const QString w = entries.at(i).toObject().value(QStringLiteral("with")).toString();
                ExtrudeInfo bi;
                if (w == uuid || !resolve(scene, w, bi) || !bi.standalone || bi.boolTarget != uuid ||
                    bi.boolRole != ExtrudeInfo::BoolConsumed)
                    continue;                           // Z 가 달라 Blender 에서 처리하는 것은 그대로
                const int op = opFromKey(entries.at(i).toObject().value(QStringLiteral("op")).toString());
                const QPainterPath pb = ringsPath(bi.rings);
                if (op == BoolUnion) {
                    shape = shape.united(pb);
                } else if (op == BoolIntersect) {
                    shape = shape.intersected(pb);
                    info.x0 = std::max(info.x0, bi.x0);
                    info.x1 = std::min(info.x1, bi.x1);
                } else if (bi.x1 > info.x0 + 1e-6 && bi.x0 < info.x1 - 1e-6) {
                    shape = shape.subtracted(pb);
                }
                changed = true;
            }
            --g_boolDepth;
            if (changed)
                info.rings = pathRings(shape);
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
    // 다시 못 찾았을 때: 선이 아직 있으면(살짝 벌어짐 등) 등록 모양을 그대로 쓰고,
    //   그 자리에 선이 남아 있지 않으면 스케치를 지우거나 옮긴 것 → 돌출도 없어진 것 (선을 되살리면 다시 나타남)
    if (stale && !outlineStillDrawn(scene, region, info.viewId))
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
