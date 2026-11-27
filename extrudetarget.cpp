#include "extrudetarget.h"

#include "cadcommands.h"
#include "iappcontext.h"
#include "mechmodel3d.h"
#include "mechpartdata.h"
#include "mygraphicsscene.h"
#include "plateview.h"
#include "projectionview.h"
#include "revolveprofile.h"
#include "scenesymbolregistry.h"

#include <QBrush>
#include <QCoreApplication>
#include <QGraphicsPathItem>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QPen>
#include <QSet>
#include <QUndoStack>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {

QString trT(const char *s) { return QCoreApplication::translate("ExtrudeTargets", s); }

// 축 표시(3D 보기)와 같은 계열 색 (어두운 배경의 도면·설정창에서도 읽히게 조금 밝게)
QColor colorX() { return QColor(232, 80, 80); }
QColor colorY() { return QColor(64, 175, 84); }
QColor colorZ() { return QColor(80, 150, 255); }

// 회전체 단면: 축 시작점·각도·축 방향 범위
bool revolveRange(MyGraphicsScene *scene, const QString &host, QPointF &a0, double &angleDeg, double &xmin, double &xmax)
{
    QList<RevolveLoop> loops;
    QString name;
    if (!MechModel3D::revolveSection(scene, host, loops, a0, angleDeg, name))
        return false;
    xmin = 1e300;
    xmax = -1e300;
    for (int i = 0; i < loops.size(); ++i)
        for (int k = 0; k < loops.at(i).pts.size(); ++k) {
            xmin = std::min(xmin, loops.at(i).pts.at(k).x());
            xmax = std::max(xmax, loops.at(i).pts.at(k).x());
        }
    return xmax > xmin;
}

// 판의 뷰 찾기
bool plateFace(MyGraphicsScene *scene, const ExtrudeTarget &t, PlateView::FaceView &fv)
{
    const QList<PlateView::FaceView> fvs = PlateView::views(scene, t.host);
    for (int i = 0; i < fvs.size(); ++i)
        if (fvs.at(i).viewId == t.viewId) {
            fv = fvs.at(i);
            return true;
        }
    return false;
}

// 돌출(파내기)이 진행하는 방향 (도면 안 단위 벡터). 도면에 수직(Z)이면 (0,0)
QPointF drawingDirection(MyGraphicsScene *scene, const ExtrudeTarget &t, bool cut)
{
    if (t.type == ExtrudeTarget::RevolveSide) {
        QPointF a0;
        double ang = 0.0, x0 = 0.0, x1 = 0.0;
        if (!revolveRange(scene, t.host, a0, ang, x0, x1))
            return QPointF();
        const double r = qDegreesToRadians(ang), s = (t.viewKind == ProjectionView::Left) ? 1.0 : -1.0;
        return QPointF(s * std::cos(r), s * std::sin(r));
    }
    if (t.type == ExtrudeTarget::PlateFace) {
        PlateView::FaceView fv;
        if (plateFace(scene, t, fv))
            return cut ? -fv.n : fv.n;
    }
    return QPointF();
}

// 방향 벡터 → 축 이름 ("+X" 등, 기울었으면 빈 글자) 과 색, 시작 쪽 이름
QString axisName(const QPointF &d, QColor &color, QString &startSide)
{
    const bool alongX = std::fabs(d.x()) >= std::fabs(d.y());
    color = alongX ? colorX() : colorY();
    startSide = trT("이 측면 쪽");
    if (std::fabs(d.x()) > 0.9999) {
        startSide = d.x() > 0.0 ? trT("왼쪽") : trT("오른쪽");
        return d.x() > 0.0 ? QStringLiteral("+X") : QStringLiteral("−X");
    }
    if (std::fabs(d.y()) > 0.9999) {
        startSide = d.y() > 0.0 ? trT("아래쪽") : trT("위쪽");
        return d.y() > 0.0 ? QStringLiteral("+Y") : QStringLiteral("−Y");
    }
    return QString();
}

// 공간이 든 판의 뷰들
void addPlateFaces(MyGraphicsScene *scene, const QPointF &pos, QList<ExtrudeTarget> &out)
{
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        if (!PlateView::isPlate(scene, it.key()))
            continue;
        const QList<PlateView::FaceView> fvs = PlateView::views(scene, it.key());
        for (int i = 0; i < fvs.size(); ++i) {
            if (!fvs.at(i).poly.containsPoint(pos, Qt::OddEvenFill))
                continue;
            ExtrudeTarget t;
            t.type = ExtrudeTarget::PlateFace;
            t.host = it.key();
            t.hostName = it.value().name;
            t.viewId = fvs.at(i).viewId;
            t.viewKind = fvs.at(i).kind;
            t.viewName = ProjectionView::kindName(t.viewKind);
            t.outline.addPolygon(fvs.at(i).poly);
            t.outline.closeSubpath();
            out << t;
        }
    }
}

// 회전체 측면도 후보 + 거리 (가까운 순 정렬용)
struct SideCandidate {
    ExtrudeTarget target;
    double dist = 0.0;
};

bool closerSide(const SideCandidate &a, const SideCandidate &b) { return a.dist < b.dist; }

// 모든 회전체 측면도 — 가까운 순. 공간과 떨어져 있으면 away (자동 판단 findHostView 와 같은 기준)
void addRevolveSides(MyGraphicsScene *scene, const QPainterPath &region, QList<ExtrudeTarget> &out)
{
    const QRectF rb = region.boundingRect();
    const double size = std::max(rb.width(), rb.height());
    QList<SideCandidate> list;
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        if (it.value().type != QLatin1String("MechRevolve"))
            continue;
        const QList<ProjectionView::SideViewInfo> svs = ProjectionView::sideViews(scene, it.key());
        for (int i = 0; i < svs.size(); ++i) {
            SideCandidate c;
            ExtrudeTarget &t = c.target;
            t.type = ExtrudeTarget::RevolveSide;
            t.host = it.key();
            t.hostName = it.value().name;
            t.viewId = svs.at(i).viewId;
            t.viewKind = svs.at(i).kind;
            t.viewName = ProjectionView::kindName(t.viewKind);
            t.concealed = svs.at(i).concealed;
            t.outline.addEllipse(svs.at(i).center, svs.at(i).rMax, svs.at(i).rMax);
            c.dist = QLineF(svs.at(i).center, rb.center()).length();
            t.away = c.dist > svs.at(i).rMax + size;
            list << c;
        }
    }
    std::stable_sort(list.begin(), list.end(), closerSide);   // 같은 거리면 먼저 찾은 것 (findHostView 와 같음)
    for (int i = 0; i < list.size(); ++i)
        out << list.at(i).target;
}

QPainterPath ringsOutline(const QList<QPolygonF> &rings)
{
    QPainterPath p;
    p.setFillRule(Qt::OddEvenFill);
    for (int i = 0; i < rings.size(); ++i) {
        p.addPolygon(rings.at(i));
        p.closeSubpath();
    }
    return p;
}

// 두 공간이 (거의) 같은가 — 원호 윤곽은 다시 찾을 때 꼭짓점이 조금 달라져 크기의 0.5 % 까지 같은 것으로 봄
bool sameArea(const QPainterPath &a, const QPainterPath &b)
{
    if (a.isEmpty() || b.isEmpty())
        return false;
    const QRectF nb = b.boundingRect();
    const double tol = std::max(0.05, 0.005 * std::max(nb.width(), nb.height()));
    if (!a.boundingRect().adjusted(-tol, -tol, tol, tol).contains(nb) ||
        !nb.adjusted(-tol, -tol, tol, tol).contains(a.boundingRect()))
        return false;
    const QRectF d1 = a.subtracted(b).boundingRect(), d2 = b.subtracted(a).boundingRect();
    return !((d1.width() >= tol && d1.height() >= tol) || (d2.width() >= tol && d2.height() >= tol));
}

ExtrudeTarget::Type recordType(const QMap<QString, QVariant> &p)
{
    if (p.value(QStringLiteral("PlateFront")).toBool())
        return ExtrudeTarget::PlateFront;
    if (p.value(QStringLiteral("PlateBoss")).toBool())
        return ExtrudeTarget::PlateFace;
    if (p.value(QStringLiteral("Standalone")).toBool())
        return ExtrudeTarget::None;
    return ExtrudeTarget::RevolveSide;
}

// 부품의 투상도 뷰 UUID 들
QSet<QString> viewIdSet(const SymbolInfo &si)
{
    QSet<QString> ids;
    const QJsonArray vj = QJsonDocument::fromJson(si.props.value(QStringLiteral("Views")).toString().toUtf8()).array();
    for (int i = 0; i < vj.size(); ++i)
        ids.insert(vj.at(i).toObject().value(QStringLiteral("id")).toString());
    ids.remove(QString());
    return ids;
}

void addPathItem(MyGraphicsScene *scene, QList<QGraphicsItem *> &out, const QPainterPath &path, const QColor &c,
                 double width, Qt::PenStyle style, bool fill)
{
    if (path.isEmpty())
        return;
    QGraphicsPathItem *it = new QGraphicsPathItem(path);
    QPen pen(c, width, style);
    pen.setCosmetic(true);                              // 확대와 무관한 굵기
    it->setPen(pen);
    it->setBrush(fill ? QBrush(c) : QBrush(Qt::NoBrush));
    it->setZValue(1e9);
    it->setAcceptedMouseButtons(Qt::NoButton);
    scene->addItem(it);
    out << it;
}

// 화살표 (p → p + d·len)
void addArrow(MyGraphicsScene *scene, QList<QGraphicsItem *> &out, const QPointF &p, const QPointF &d, double len,
              const QColor &c)
{
    const QPointF n(-d.y(), d.x()), q = p + d * len;
    const double h = len * 0.25;
    QPainterPath line, head;
    line.moveTo(p);
    line.lineTo(q - d * h);
    QPolygonF tri;
    tri << q << (q - d * h + n * (h * 0.4)) << (q - d * h - n * (h * 0.4));
    head.addPolygon(tri);
    head.closeSubpath();
    addPathItem(scene, out, line, c, 3.0, Qt::SolidLine, false);
    addPathItem(scene, out, head, c, 1.0, Qt::SolidLine, true);
}

} // namespace

QString ExtrudeTarget::label() const
{
    if (type == None)
        return trT("없음 (새 부품)");
    QString s = QStringLiteral("%1 — %2").arg(hostName, viewName);
    if (concealed)
        s += trT(" (숨김)");
    return s;
}

QList<ExtrudeTarget> ExtrudeTargets::forExtrude(MyGraphicsScene *scene, const QPainterPath &region, const QPointF &probe)
{
    QList<ExtrudeTarget> sides, out;
    if (!scene || region.isEmpty())
        return out;
    addPlateFaces(scene, region.boundingRect().center(), out);
    addRevolveSides(scene, region, sides);
    // 없음 — 이미 돌출된 판 위에 그린 공간이면 시작 Z = 그 판(겹치면 가장 앞)의 윗면
    //   지금 고른 공간과 같은 공간인 판은 뺀다: 그 판은 "아래 판"이 아니라 같은 스케치의 옛 기록
    //   (투상도·3D 만 지우고 기록이 남았거나, 다시 돌출하는 경우 — 등록 때 removeSameStandalone 이 대체함)
    ExtrudeTarget none;
    QStringList sameSpace;
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        if (it.value().type != QLatin1String("MechExtrude") || recordType(it.value().props) != ExtrudeTarget::None)
            continue;
        if (sameArea(ExtrudeFeature::currentArea(scene, it.key()), region))
            sameSpace << it.key();
    }
    const QString below = PlateView::plateAt(scene, probe, true, sameSpace);
    ExtrudeInfo bi;
    if (!below.isEmpty() && ExtrudeFeature::resolve(scene, below, bi)) {
        none.startZ = bi.x1;
        none.startNote = trT("※ 아래 판 '%1' 윗면 높이 = %2 자동 적용").arg(bi.name).arg(bi.x1);
    }
    // 떨어진 측면도는 자동 판단에 쓰지 않으므로 "없음" 뒤로
    for (int i = 0; i < sides.size(); ++i)
        if (!sides.at(i).away)
            out << sides.at(i);
    out << none;
    for (int i = 0; i < sides.size(); ++i)
        if (sides.at(i).away)
            out << sides.at(i);
    return out;
}

QList<ExtrudeTarget> ExtrudeTargets::forCut(MyGraphicsScene *scene, const QPainterPath &region, const QPointF &probe)
{
    QList<ExtrudeTarget> sides, out;
    if (!scene || region.isEmpty())
        return out;
    addPlateFaces(scene, probe, out);
    const QString plate = PlateView::plateAt(scene, probe, true);
    ExtrudeInfo pe;
    if (!plate.isEmpty() && ExtrudeFeature::resolve(scene, plate, pe)) {
        ExtrudeTarget t;
        t.type = ExtrudeTarget::PlateFront;
        t.host = plate;
        t.hostName = pe.name;
        t.viewName = trT("정면");
        t.outline = ringsOutline(pe.rings);
        out << t;
    }
    addRevolveSides(scene, region, sides);
    // 파낼 공간 근처에 부품이 하나도 없으면 빈 목록 (지금처럼 "파낼 부품을 찾지 못함")
    bool any = !out.isEmpty();
    for (int i = 0; i < sides.size() && !any; ++i)
        any = !sides.at(i).away;
    if (any) {
        // 떨어진 측면도는 맨 뒤로 (가까워도 공간보다 작은 측면도가 자동값이 되지 않게)
        for (int i = 0; i < sides.size(); ++i)
            if (!sides.at(i).away)
                out << sides.at(i);
        for (int i = 0; i < sides.size(); ++i)
            if (sides.at(i).away)
                out << sides.at(i);
    }
    return out;
}

QString ExtrudeTargets::directionText(MyGraphicsScene *scene, const ExtrudeTarget &t, bool cut, QColor *axisColor)
{
    QColor c = colorZ();
    QString text;
    if (t.type == ExtrudeTarget::None) {
        text = trT("<b>+Z</b> (도면에 수직, 보는 쪽)");
    } else if (t.type == ExtrudeTarget::PlateFront) {
        text = trT("<b>−Z</b> (앞면에서 안쪽으로)");
    } else {
        QString side;
        const QString ax = axisName(drawingDirection(scene, t, cut), c, side);
        if (t.type == ExtrudeTarget::RevolveSide)
            text = ax.isEmpty() ? trT("<b>회전체 축 방향</b> — 이 측면 쪽 끝면에서 안쪽으로")
                                : trT("<b>%1</b> (회전체 축) — %2 끝면에서 안쪽으로").arg(ax, side);
        else if (cut)
            text = ax.isEmpty() ? trT("<b>면에 수직</b> — 안쪽으로") : trT("<b>%1</b> (면에서 안쪽으로)").arg(ax);
        else
            text = ax.isEmpty() ? trT("<b>면에 수직</b> — 바깥쪽으로") : trT("<b>%1</b> (면에서 바깥쪽으로)").arg(ax);
    }
    if (axisColor)
        *axisColor = c;
    return QStringLiteral("<span style=\"color:%1\">%2</span>").arg(c.name(), text);
}

QList<QGraphicsItem *> ExtrudeTargets::addPreview(MyGraphicsScene *scene, const ExtrudeTarget &t,
                                                  const QPainterPath &region, const ExtrudeValues &v)
{
    QList<QGraphicsItem *> out;
    if (!scene)
        return out;
    addPathItem(scene, out, t.outline, QColor(240, 130, 20), 3.0, Qt::SolidLine, false);
    QColor axis;
    directionText(scene, t, v.cut, &axis);

    const QRectF rb = region.boundingRect();
    if (t.type == ExtrudeTarget::None || t.type == ExtrudeTarget::PlateFront) {
        // 도면에 수직: 보는 쪽으로 나오면 ⊙, 들어가면 ⊗
        const double r = std::max(1e-3, 0.12 * std::min(rb.width(), rb.height()));
        const QPointF c = rb.center();
        QPainterPath ring, mark;
        ring.addEllipse(c, r, r);
        addPathItem(scene, out, ring, axis, 2.5, Qt::SolidLine, false);
        if (t.type == ExtrudeTarget::None) {
            mark.addEllipse(c, r * 0.25, r * 0.25);
            addPathItem(scene, out, mark, axis, 1.0, Qt::SolidLine, true);
        } else {
            const double k = r * std::sqrt(0.5);
            mark.moveTo(c + QPointF(-k, -k));
            mark.lineTo(c + QPointF(k, k));
            mark.moveTo(c + QPointF(-k, k));
            mark.lineTo(c + QPointF(k, -k));
            addPathItem(scene, out, mark, axis, 2.5, Qt::SolidLine, false);
        }
        return out;
    }

    // 도면 안 방향: 정면도 위의 돌출 범위 사각 (진행 방향 x0~x1 × 가로 y0~y1)
    QPointF a0;
    double ang = 0.0, x0 = 0.0, x1 = 0.0, y0 = 1e300, y1 = -1e300;
    bool forward = true;                                // x0 → x1 로 진행
    if (t.type == ExtrudeTarget::RevolveSide) {
        double xmin = 0.0, xmax = 0.0;
        if (!revolveRange(scene, t.host, a0, ang, xmin, xmax))
            return out;
        forward = (t.viewKind == ProjectionView::Left);
        x0 = forward ? xmin + v.offset : xmax - v.offset - v.depth;
        x1 = x0 + v.depth;
        const QList<ProjectionView::SideViewInfo> svs = ProjectionView::sideViews(scene, t.host);
        const QList<QPolygonF> polys = region.toSubpathPolygons();
        for (int i = 0; i < svs.size(); ++i) {
            if (svs.at(i).viewId != t.viewId)
                continue;
            for (int p = 0; p < polys.size(); ++p)
                for (int k = 0; k < polys.at(p).size(); ++k) {
                    const QPointF d = polys.at(p).at(k) - svs.at(i).center;
                    const double dn = d.x() * svs.at(i).n.x() + d.y() * svs.at(i).n.y();
                    y0 = std::min(y0, dn);
                    y1 = std::max(y1, dn);
                }
        }
    } else {
        ExtrudeInfo info;
        info.cut = v.cut;
        info.through = v.through;
        if (!PlateView::bossSection(scene, t.host, t.viewId, region, v.depth, false, info))
            return out;
        a0 = info.a0;
        ang = info.angleDeg;
        x0 = info.x0;
        x1 = info.x1;
        forward = !v.cut;
        for (int r = 0; r < info.rings.size(); ++r)
            for (int k = 0; k < info.rings.at(r).size(); ++k) {
                y0 = std::min(y0, info.rings.at(r).at(k).x());
                y1 = std::max(y1, info.rings.at(r).at(k).x());
            }
    }
    if (y1 < y0)
        return out;
    const double rad = qDegreesToRadians(ang);
    const QPointF u(std::cos(rad), std::sin(rad)), n(-std::sin(rad), std::cos(rad));
    QPolygonF box;
    box << a0 + u * x0 + n * y0 << a0 + u * x1 + n * y0 << a0 + u * x1 + n * y1 << a0 + u * x0 + n * y1;
    QPainterPath bp;
    bp.addPolygon(box);
    bp.closeSubpath();
    addPathItem(scene, out, bp, colorZ(), 2.0, Qt::DashLine, false);
    const double mid = (y0 + y1) * 0.5;
    addArrow(scene, out, a0 + u * (forward ? x0 : x1) + n * mid, forward ? u : -u, x1 - x0, axis);
    return out;
}

void ExtrudeTargets::removePreview(MyGraphicsScene *scene, QList<QGraphicsItem *> &items)
{
    for (int i = 0; i < items.size(); ++i) {
        if (scene)
            scene->removeItem(items.at(i));
        delete items.at(i);
    }
    items.clear();
}

QStringList ExtrudeTargets::otherRecords(MyGraphicsScene *scene, const QPainterPath &region, const ExtrudeTarget &t,
                                         bool cut)
{
    QStringList out;
    if (!scene)
        return out;
    const QMap<QString, SymbolInfo> &all = scene->symbolRegistry()->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        const QMap<QString, QVariant> &p = it.value().props;
        if (it.value().type != QLatin1String("MechExtrude") || it.key() == t.host ||
            p.value(QStringLiteral("Cut")).toBool() != cut)
            continue;
        // 같은 대상이면 등록 함수가 알아서 바꿔 준다 (회전체는 같은 방향의 다른 측면도 포함)
        //   판의 뷰는 바꿔 주는 처리가 없으므로 같은 뷰라도 물어본다
        const ExtrudeTarget::Type rt = recordType(p);
        if (rt == t.type) {
            if (rt == ExtrudeTarget::None)
                continue;
            if (rt == ExtrudeTarget::RevolveSide && p.value(QStringLiteral("Host")).toString() == t.host &&
                p.value(QStringLiteral("ViewKind")).toInt() == t.viewKind)
                continue;
        }
        // 지금 도면에서 그 기록이 나타내는 공간
        if (sameArea(ExtrudeFeature::currentArea(scene, it.key()), region))
            out << it.key();
    }
    return out;
}

QString ExtrudeTargets::recordLabel(MyGraphicsScene *scene, const QString &uuid)
{
    const SymbolInfo si = scene->symbolRegistry()->symbolInfo(uuid);
    const ExtrudeTarget::Type rt = recordType(si.props);
    if (rt == ExtrudeTarget::None) {
        QString s = trT("'%1' (새 부품, Z 방향)").arg(si.name);
        const int nv = int(viewIdSet(si).size());
        const int na = int(ExtrudeFeature::attachedTo(scene, uuid).size());
        if (nv > 0)
            s += trT(" — 투상도 %1개 포함").arg(nv);
        if (na > 0)
            s += trT(" — 붙은 돌기·파내기 %1개 포함").arg(na);
        return s;
    }
    const QString host = si.props.value(QStringLiteral("Host")).toString();
    const QString hostName = scene->symbolRegistry()->hasSymbol(host) ? scene->symbolRegistry()->symbolInfo(host).name
                                                                      : QStringLiteral("?");
    const QString view = rt == ExtrudeTarget::PlateFront
                             ? trT("정면")
                             : ProjectionView::kindName(si.props.value(QStringLiteral("ViewKind")).toInt());
    return QStringLiteral("'%1' (%2 — %3)").arg(si.name, hostName, view);
}

void ExtrudeTargets::removeRecord(IAppContext *ctx, MyGraphicsScene *scene, const QString &uuid)
{
    if (!ctx || !scene || !scene->symbolRegistry()->hasSymbol(uuid))
        return;
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    QUndoStack *undo = ctx->undoStack();
    const SymbolInfo si = reg->symbolInfo(uuid);
    // 새 부품(판)이면 붙은 돌기·파내기와 투상도 선도 지움 (남기면 주인 없는 선이 됨)
    QStringList gone;
    if (recordType(si.props) == ExtrudeTarget::None)
        gone = ExtrudeFeature::attachedTo(scene, uuid);
    const QSet<QString> views = viewIdSet(si);
    if (!views.isEmpty()) {
        const QList<QGraphicsItem *> items = scene->items();
        for (int i = 0; i < items.size(); ++i) {
            if (!views.contains(items.at(i)->data(MECH_VIEW_ID).toString()))
                continue;
            if (undo)
                undo->push(new RemoveEntityCommand(scene, items.at(i)));
            else
                scene->removeItem(items.at(i));
        }
    }
    gone << uuid;
    for (int i = 0; i < gone.size(); ++i) {
        const SymbolInfo gi = reg->symbolInfo(gone.at(i));
        if (undo)
            undo->push(new UnregisterSymbolCommand(reg, gone.at(i), gi));
        else
            reg->unregisterSymbol(gone.at(i));
    }
    // 붙어 있던 부품의 투상도 갱신 (돌출 선 사라짐)
    const QString host = si.props.value(QStringLiteral("Host")).toString();
    if (!host.isEmpty() && reg->hasSymbol(host))
        ProjectionView::regeneratePart(ctx, scene, host);
    ProjectionView::applyConcealedHoles(scene);
}
