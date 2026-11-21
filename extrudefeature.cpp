#include "extrudefeature.h"

#include "mechmodel3d.h"
#include "mechparttag.h"
#include "mygraphicsscene.h"
#include "projectionview.h"
#include "revolveprofile.h"
#include "scenesymbolregistry.h"

#include <QJsonArray>
#include <QJsonDocument>
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

} // namespace

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

    // 공간 다시 찾기 (투상 원도 경계)
    const QJsonArray b = QJsonDocument::fromJson(pr.value(QStringLiteral("Box")).toString().toUtf8()).array();
    const QRectF box(b.at(0).toDouble(), b.at(1).toDouble(), b.at(2).toDouble(), b.at(3).toDouble());
    QList<QPointF> picks;
    const QJsonArray pj = QJsonDocument::fromJson(pr.value(QStringLiteral("Picks")).toString().toUtf8()).array();
    for (int i = 0; i < pj.size(); ++i)
        picks << QPointF(pj.at(i).toArray().at(0).toDouble(), pj.at(i).toArray().at(1).toDouble());
    QPainterPath region = RevolveProfile::areaRegion(scene, box, picks, true);
    if (region.isEmpty())
        region = regionFromJson(pr.value(QStringLiteral("Region")).toString());
    if (region.isEmpty())
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
    // 바깥 = 반시계, 안쪽 구멍 = 시계 (다른 고리 안에 든 횟수)
    info.rings.clear();
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
        info.rings << r;
    }
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

QString ExtrudeFeature::registerExtrude(MyGraphicsScene *scene, QUndoStack *undo, const QString &host,
                                        const QString &viewId, int viewKind, const QRectF &box,
                                        const QList<QPointF> &picks, const QPainterPath &region,
                                        const QString &name, double offset, double depth, bool merge,
                                        QString *error)
{
    if (!scene || region.isEmpty() || host.isEmpty()) {
        if (error)
            *error = QStringLiteral("돌출할 공간 또는 측면도를 찾지 못했습니다.");
        return QString();
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
    props[QStringLiteral("Merge")] = merge;
    const QPointF ref = region.boundingRect().topLeft();
    const QString uuid = MechPartTag::registerPart(scene, undo, nullptr, QStringLiteral("MechExtrude"), name, props,
                                                   ref, 0.0, &ref);
    if (uuid.isEmpty() && error)
        *error = QStringLiteral("부품 등록에 실패했습니다.");
    return uuid;
}
