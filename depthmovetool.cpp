#include "depthmovetool.h"

#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "extrudefeature.h"
#include "hatchitem.h"
#include "iappcontext.h"
#include "mechmodel3d.h"
#include "mechpartdata.h"
#include "mygraphicsscene.h"
#include "projectionview.h"
#include "revolveprofile.h"
#include "scenesymbolregistry.h"
#include "snapmanager.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMessageBox>
#include <QUndoStack>
#include <QtMath>
#include <cmath>

namespace {

double distToPath(const QPointF &p, const QList<QPolygonF> &polys)
{
    double best = 1e300;
    for (int i = 0; i < polys.size(); ++i) {
        const QPolygonF &pg = polys.at(i);
        for (int k = 0; k + 1 < pg.size(); ++k) {
            const QPointF a = pg.at(k), b = pg.at(k + 1);
            const QPointF d = b - a;
            const double L2 = d.x() * d.x() + d.y() * d.y();
            double t = L2 > 1e-18 ? ((p.x() - a.x()) * d.x() + (p.y() - a.y()) * d.y()) / L2 : 0.0;
            t = std::max(0.0, std::min(1.0, t));
            best = std::min(best, QLineF(p, a + d * t).length());
        }
    }
    return best;
}

// 단면 경계 위에 놓인 선(태그 없는 일반 도형)을 찾는다 — 해칭과 함께 옮길 선
QList<CadItem *> boundaryItems(MyGraphicsScene *scene, const QList<QPainterPath> &paths)
{
    QList<CadItem *> out;
    QList<QPolygonF> polys;
    QRectF box;
    for (int i = 0; i < paths.size(); ++i) {
        polys << paths.at(i).toSubpathPolygons();
        box = box.isNull() ? paths.at(i).boundingRect() : box.united(paths.at(i).boundingRect());
    }
    const QList<QGraphicsItem *> items = scene->items(box.adjusted(-0.5, -0.5, 0.5, 0.5), Qt::IntersectsItemShape);
    for (int i = 0; i < items.size(); ++i) {
        CadItem *ci = dynamic_cast<CadItem *>(items.at(i));
        if (!ci || dynamic_cast<HatchItem *>(ci))
            continue;
        if (!ci->data(MECH_DATA_PART_ID).toString().isEmpty() || !ci->data(MECH_VIEW_ID).toString().isEmpty())
            continue;                                   // 다른 부품의 기준 도형·투상도 선은 제외
        const QList<QPolygonF> gp = ci->sceneTransform().map(ci->geometryPath()).toSubpathPolygons();
        bool allOn = !gp.isEmpty();
        for (int g = 0; g < gp.size() && allOn; ++g)
            for (int k = 0; k < gp.at(g).size() && allOn; ++k) {
                allOn = distToPath(gp.at(g).at(k), polys) < 1e-3;
                if (allOn && k + 1 < gp.at(g).size())
                    allOn = distToPath((gp.at(g).at(k) + gp.at(g).at(k + 1)) * 0.5, polys) < 1e-3;
            }
        if (allOn)
            out << ci;
    }
    return out;
}

QString shiftRectJson(const QString &json, const QPointF &d)
{
    const QJsonArray b = QJsonDocument::fromJson(json.toUtf8()).array();
    if (b.size() != 4)
        return json;
    return QString::fromUtf8(QJsonDocument(QJsonArray{b.at(0).toDouble() + d.x(), b.at(1).toDouble() + d.y(),
                                                      b.at(2).toDouble(), b.at(3).toDouble()})
                                 .toJson(QJsonDocument::Compact));
}

QString shiftPointsJson(const QString &json, const QPointF &d)
{
    const QJsonArray a = QJsonDocument::fromJson(json.toUtf8()).array();
    QJsonArray out;
    for (int i = 0; i < a.size(); ++i)
        out.append(QJsonArray{a.at(i).toArray().at(0).toDouble() + d.x(), a.at(i).toArray().at(1).toDouble() + d.y()});
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString shiftPolysJson(const QString &json, const QPointF &d)
{
    const QJsonArray polys = QJsonDocument::fromJson(json.toUtf8()).array();
    QJsonArray out;
    for (int i = 0; i < polys.size(); ++i)
        out.append(QJsonDocument::fromJson(shiftPointsJson(
                                               QString::fromUtf8(QJsonDocument(polys.at(i).toArray()).toJson()), d)
                                               .toUtf8())
                       .array());
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

// 기준 t 에 대해 [f0, f1] 범위 부품의 "기준 쪽 끝면"을 depth 만큼 떨어뜨리는 이동량
double moveDelta(double f0, double f1, double tRef, double depth)
{
    const double center = (f0 + f1) * 0.5;
    if (center >= tRef)
        return (tRef + depth) - f0;   // 부품이 기준 + 쪽 → 왼쪽 끝면을 기준 + depth 로
    return (tRef - depth) - f1;       // 부품이 기준 − 쪽 → 오른쪽 끝면을 기준 − depth 로
}

} // namespace

QString DepthMove::partAt(MyGraphicsScene *scene, const QPointF &pos, bool &isExtrude)
{
    isExtrude = false;
    if (!scene)
        return QString();
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    // 정면·평면도의 돌출 선
    const QList<QGraphicsItem *> items = scene->items(QRectF(pos.x() - 1.0, pos.y() - 1.0, 2.0, 2.0));
    for (int i = 0; i < items.size(); ++i) {
        const QString id = items.at(i)->data(MECH_EXTRUDE_ID).toString();
        if (!id.isEmpty() && reg->hasSymbol(id)) {
            isExtrude = true;
            return id;
        }
    }
    // 측면도에 그린 돌출 공간
    const QMap<QString, SymbolInfo> &all = reg->allSymbols();
    for (QMap<QString, SymbolInfo>::const_iterator it = all.constBegin(); it != all.constEnd(); ++it) {
        if (it.value().type != QLatin1String("MechExtrude"))
            continue;
        const QMap<QString, QVariant> &pr = it.value().props;
        const QJsonArray b = QJsonDocument::fromJson(pr.value(QStringLiteral("Box")).toString().toUtf8()).array();
        const QRectF box(b.at(0).toDouble(), b.at(1).toDouble(), b.at(2).toDouble(), b.at(3).toDouble());
        QList<QPointF> picks;
        const QJsonArray pj = QJsonDocument::fromJson(pr.value(QStringLiteral("Picks")).toString().toUtf8()).array();
        for (int k = 0; k < pj.size(); ++k)
            picks << QPointF(pj.at(k).toArray().at(0).toDouble(), pj.at(k).toArray().at(1).toDouble());
        if (RevolveProfile::areaRegion(scene, box, picks, true).contains(pos)) {
            isExtrude = true;
            return it.key();
        }
    }
    // 단면 회전체 (해칭 태그·영역 공간)
    const QString id = ProjectionView::partAt(scene, pos);
    if (!id.isEmpty() && reg->symbolInfo(id).type == QLatin1String("MechRevolve"))
        return id;
    return QString();
}

bool DepthMove::apply(IAppContext *ctx, MyGraphicsScene *scene, const QString &uuid, bool isExtrude,
                      const QPointF &refPoint, double depth, double *deltaOut, QString *error)
{
    SceneSymbolRegistry *reg = scene->symbolRegistry();
    if (!reg->hasSymbol(uuid)) {
        if (error)
            *error = QStringLiteral("부품을 찾지 못했습니다.");
        return false;
    }
    QUndoStack *undo = ctx ? ctx->undoStack() : nullptr;

    if (isExtrude) {
        ExtrudeInfo ei;
        if (!ExtrudeFeature::resolve(scene, uuid, ei)) {
            if (error)
                *error = QStringLiteral("돌출 단면을 찾지 못했습니다.");
            return false;
        }
        if (ei.standalone) {
            if (error)
                *error = QStringLiteral("단독 돌출(판·블록)은 아직 깊이 이동을 지원하지 않습니다. 돌출 설정의 시작 Z 로 위치를 정하세요.");
            return false;
        }
        const double ang = qDegreesToRadians(ei.angleDeg);
        const QPointF u(std::cos(ang), std::sin(ang));
        const double tRef = (refPoint - ei.a0).x() * u.x() + (refPoint - ei.a0).y() * u.y();
        const double delta = moveDelta(ei.x0, ei.x1, tRef, depth);
        const SymbolInfo oldInfo = reg->symbolInfo(uuid);
        SymbolInfo info = oldInfo;
        const bool left = (ei.viewKind == ProjectionView::Left);
        info.props[QStringLiteral("Offset")] = ei.offset + (left ? delta : -delta);
        if (undo)
            undo->push(new UpdateRegistryCommand(reg, uuid, oldInfo, info));
        else
            reg->registerSymbol(uuid, info);
        ProjectionView::regeneratePart(ctx, scene, ei.host);
        if (deltaOut)
            *deltaOut = delta;
        return true;
    }

    // 단면 회전체
    QList<RevolveLoop> loops;
    QPointF a0, ref;
    double angleDeg = 0.0;
    QString name;
    if (!MechModel3D::revolveSection(scene, uuid, loops, a0, angleDeg, name, &ref)) {
        if (error)
            *error = QStringLiteral("회전 단면을 찾지 못했습니다.");
        return false;
    }
    double xmin = 1e300, xmax = -1e300;
    for (int i = 0; i < loops.size(); ++i)
        for (int k = 0; k < loops.at(i).pts.size(); ++k) {
            xmin = std::min(xmin, loops.at(i).pts.at(k).x());
            xmax = std::max(xmax, loops.at(i).pts.at(k).x());
        }
    const double ang = qDegreesToRadians(angleDeg);
    const QPointF u(std::cos(ang), std::sin(ang));
    const double tRef = (refPoint - a0).x() * u.x() + (refPoint - a0).y() * u.y();
    const double delta = moveDelta(xmin, xmax, tRef, depth);
    const QPointF d = u * delta;
    if (deltaOut)
        *deltaOut = delta;
    if (std::fabs(delta) < 1e-9)
        return true;

    // 옮길 도형: 태그된 해칭 + 단면 경계선
    QList<QPainterPath> paths;
    QPointF pref;
    RevolveProfile::partPaths(scene, uuid, paths, pref);
    QList<CadItem *> movers = boundaryItems(scene, paths);
    const QList<QGraphicsItem *> all = scene->items();
    for (int i = 0; i < all.size(); ++i)
        if (all.at(i)->data(MECH_DATA_PART_ID).toString() == uuid)
            if (CadItem *ci = dynamic_cast<CadItem *>(all.at(i)))
                if (!movers.contains(ci))
                    movers << ci;

    const SymbolInfo oldInfo = reg->symbolInfo(uuid);
    SymbolInfo info = oldInfo;
    const bool area = oldInfo.props.value(QStringLiteral("Source")).toString() == QLatin1String("area");
    if (area) {
        // 영역 방식: 저장된 사각·점·모양·축 시작점도 같이 이동 (기준점은 고정값이라 측면도는 제자리)
        info.props[QStringLiteral("Box")] = shiftRectJson(oldInfo.props.value(QStringLiteral("Box")).toString(), d);
        info.props[QStringLiteral("Picks")] = shiftPointsJson(oldInfo.props.value(QStringLiteral("Picks")).toString(), d);
        info.props[QStringLiteral("Region")] = shiftPolysJson(oldInfo.props.value(QStringLiteral("Region")).toString(), d);
        info.props[QStringLiteral("ox")] = oldInfo.props.value(QStringLiteral("ox")).toDouble() + d.x();
        info.props[QStringLiteral("oy")] = oldInfo.props.value(QStringLiteral("oy")).toDouble() + d.y();
    } else {
        // 해칭 방식: 기준점(해칭 위치)이 같이 움직이므로, 측면도가 따라가지 않게 뷰 기록의 rx/ry 를 보정
        QJsonArray views = QJsonDocument::fromJson(oldInfo.props.value(QStringLiteral("Views")).toString().toUtf8()).array();
        for (int i = 0; i < views.size(); ++i) {
            QJsonObject v = views.at(i).toObject();
            const int kind = v.value(QStringLiteral("kind")).toInt();
            if ((kind == ProjectionView::Left || kind == ProjectionView::Right) && v.contains(QStringLiteral("rx"))) {
                v.insert(QStringLiteral("rx"), v.value(QStringLiteral("rx")).toDouble() + d.x());
                v.insert(QStringLiteral("ry"), v.value(QStringLiteral("ry")).toDouble() + d.y());
                views[i] = v;
            }
        }
        info.props[QStringLiteral("Views")] = QString::fromUtf8(QJsonDocument(views).toJson(QJsonDocument::Compact));
    }
    if (undo)
        undo->push(new UpdateRegistryCommand(reg, uuid, oldInfo, info));
    else
        reg->registerSymbol(uuid, info);
    for (int i = 0; i < movers.size(); ++i) {
        if (undo)
            undo->push(new MoveEntityCommand(movers.at(i), MyVector(d.x(), d.y())));
        else
            movers.at(i)->move(MyVector(d.x(), d.y()));
    }
    ProjectionView::regeneratePart(ctx, scene, uuid);
    return true;
}

// ── 도구 ──
DepthMoveTool::DepthMoveTool(QObject *parent) : AbstractTool(parent) {}

void DepthMoveTool::activate(MyGraphicsScene *scene)
{
    AbstractTool::activate(scene);
    m_state = PickPart;
    m_part.clear();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("DEPTH3D (깊이 이동)"), CommandDock::Command);
    }
    showPrompt();
}

void DepthMoveTool::showPrompt()
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock())
        return;
    if (m_state == PickPart)
        ctx->commandDock()->showInteractivePrompt(
            tr(" 움직일 부품을 선택하세요 (정면도의 단면 회전체·돌출 선, 측면도의 돌출 공간) : "), {});
    else if (m_state == PickRef)
        ctx->commandDock()->showInteractivePrompt(tr(" 기준면: 정면도/평면도에서 점·선을 지정하세요 : "), {});
    else
        ctx->commandDock()->showInteractivePrompt(tr(" 기준면에서 떨어질 깊이 (mm) <0> : "), {});
}

void DepthMoveTool::cancelTool()
{
    emit toolFinished();
}

void DepthMoveTool::keyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        cancelTool();
        return;
    }
    if (m_state == EnterDepth && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        runWith(0.0);
        return;
    }
    AbstractTool::keyPress(event);
}

bool DepthMoveTool::processCommand(const QString &command)
{
    if (m_state != EnterDepth)
        return false;
    const QString c = command.trimmed();
    if (c.isEmpty()) {
        runWith(0.0);
        return true;
    }
    bool ok = false;
    const double v = c.toDouble(&ok);
    if (!ok)
        return false;
    runWith(v);
    return true;
}

void DepthMoveTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene)
        return;
    if (button == Qt::RightButton) {
        cancelTool();
        return;
    }
    if (button != Qt::LeftButton)
        return;
    IAppContext *ctx = getContext();
    if (m_state == PickPart) {
        m_part = DepthMove::partAt(m_scene, pos, m_isExtrude);
        if (m_part.isEmpty()) {
            if (ctx && ctx->commandDock())
                ctx->commandDock()->logMessage(tr("단면 회전체나 돌출 부품을 찾지 못했습니다."), CommandDock::Warning);
            return;
        }
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(
                tr("'%1' 선택").arg(m_scene->symbolRegistry()->symbolInfo(m_part).name), CommandDock::Info);
        m_state = PickRef;
        showPrompt();
        return;
    }
    if (m_state == PickRef) {
        m_ref = pos;
        if (ctx && ctx->snapManager())
            m_ref = ctx->snapManager()->snapPoint(pos, m_scene, QPointF()).snappedPos;
        m_state = EnterDepth;
        showPrompt();
    }
}

void DepthMoveTool::runWith(double depth)
{
    IAppContext *ctx = getContext();
    QUndoStack *undo = ctx ? ctx->undoStack() : nullptr;
    if (undo)
        undo->beginMacro(tr("깊이 이동"));
    double delta = 0.0;
    QString err;
    const bool ok = DepthMove::apply(ctx, m_scene, m_part, m_isExtrude, m_ref, depth, &delta, &err);
    if (undo)
        undo->endMacro();
    if (!ok)
        QMessageBox::warning(QApplication::activeWindow(), tr("깊이 이동"), err);
    else if (ctx && ctx->commandDock())
        ctx->commandDock()->logMessage(tr("축 방향으로 %1 mm 이동했습니다.").arg(delta, 0, 'f', 3), CommandDock::Info);
    cancelTool();
}
