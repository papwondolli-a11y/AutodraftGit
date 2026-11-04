#include "circlecentertool.h"
#include "drawingsettingscommand.h"

#include "cadcommands.h"
#include "caditem.h"
#include "circleitem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "lineitem.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include <QKeyEvent>
#include <QPainterPath>
#include <QUndoStack>
#include <QtMath>

CircleCenterTool::CircleCenterTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitingSelect), m_isDiameterMode(false) {}

CircleCenterTool::~CircleCenterTool() {}

void CircleCenterTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene)
        return;
    AbstractTool::activate(scene);
    m_state = WaitingSelect;
    m_isDiameterMode = false;
    if (m_scene) {
        hideGripsOfSelectedItems();
        m_scene->clearSelection();
    }
    updatePrompt();
}

void CircleCenterTool::deactivate() {
    if (m_scene) {
        m_scene->clearOverlay();
        m_scene->clearSelection();
    }
    AbstractTool::deactivate();
}

void CircleCenterTool::onFinish() {
    if (m_scene)
        m_scene->clearOverlay();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(false);
        ctx->commandDock()->setPrompt(" Command : ");
    }
}

void CircleCenterTool::onCancel() {
    cancelRadiusDrawing();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->logMessage("*Cancel*", CommandDock::Info);
        ctx->commandDock()->setToolActive(false);
        ctx->commandDock()->setPrompt(" Command : ");
    }
}

void CircleCenterTool::updatePrompt() {
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock())
        return;

    ctx->commandDock()->setToolActive(true);
    ctx->commandDock()->logMessage("CIRCLECENTER", CommandDock::Command);

    if (m_state == WaitingSelect) {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 원을 선택하거나 중심점을 지정하세요 : "), {});
    } else {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 반지름(R)값 입력 또는 화면에서 지정하세요 or [Diameter] : "),
            {"Diameter(D)"});
    }
}

bool CircleCenterTool::processCommand(const QString &command) {
    if (m_state != DrawingRadius)
        return false;

    const QString cmd = command.trimmed().toLower();

    if (cmd == "d") {
        m_isDiameterMode = true;
        IAppContext *ctx = getContext();
        if (ctx && ctx->commandDock())
            ctx->commandDock()->showInteractivePrompt(
                tr(" 지름(⌀)값 입력 또는 화면에서 지정하세요 : "), {});
        return true;
    }

    bool ok = false;
    double val = cmd.toDouble(&ok);
    if (ok && val > 0) {
        double radius = m_isDiameterMode ? val / 2.0 : val;
        finishCircleAndCenter(radius);
        return true;
    }

    return false;
}

void CircleCenterTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene)
        return;

    if (button == Qt::RightButton) {
        if (m_state == DrawingRadius) {
            cancelRadiusDrawing();
            updatePrompt();
        } else {
            onCancel();
            emit toolFinished();
        }
        return;
    }
    if (button != Qt::LeftButton)
        return;

    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state == DrawingRadius) ? m_centerPoint : QPointF();
        snapped = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    }

    if (m_state == WaitingSelect) {
        QGraphicsItem *rawItem = itemAtPos(pos);
        CircleItem *ci = dynamic_cast<CircleItem *>(rawItem);
        if (!ci && rawItem)
            ci = dynamic_cast<CircleItem *>(rawItem->parentItem());

        if (ci) {
            MyCircle circle = ci->getCircle();
            addCenterLines(circle.center.toPointF(), circle.radius);
            updatePrompt();
        } else {
            m_centerPoint    = snapped;
            m_currentMousePos = snapped;
            m_isDiameterMode  = false;
            m_state           = DrawingRadius;
            if (m_scene)
                m_scene->setGhostPath(QPainterPath(), true);
            updatePrompt();
        }
    } else {
        m_currentMousePos = snapped;
        double dist   = QLineF(m_centerPoint, snapped).length();
        double radius = m_isDiameterMode ? dist / 2.0 : dist;
        finishCircleAndCenter(radius);
    }
}

void CircleCenterTool::mouseMove(QPointF pos) {
    if (!m_scene)
        return;

    IAppContext *ctx = getContext();
    QPointF snapped  = pos;
    SnapType snapType = SnapType::None;
    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state == DrawingRadius) ? m_centerPoint : QPointF();
        auto res = ctx->snapManager()->snapPoint(pos, m_scene, base);
        snapped  = res.snappedPos;
        snapType = res.type;
    }
    m_currentMousePos = snapped;
    m_scene->setSnapMarker(snapped, snapType);

    if (m_state == DrawingRadius) {
        double dist   = QLineF(m_centerPoint, snapped).length();
        double radius = m_isDiameterMode ? dist / 2.0 : dist;
        QPainterPath path;
        path.addEllipse(m_centerPoint, radius, radius);
        updateGhostPreview(path, currentLayerName());
    }
}

void CircleCenterTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && m_state == DrawingRadius) {
        cancelRadiusDrawing();
        updatePrompt();
        return;
    }
    AbstractTool::keyPress(event);
}

void CircleCenterTool::cancelRadiusDrawing() {
    m_state          = WaitingSelect;
    m_isDiameterMode = false;
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
}

// ── 중심선 생성 (기존 원 선택 시) ─────────────────────────────────────────────

void CircleCenterTool::addCenterLines(const QPointF &center, double radius) {
    if (!m_scene || radius < 1e-6)
        return;

    IAppContext *ctx = getContext();
    if (!ctx)
        return;

    Layer *layer = nullptr;
    if (ctx->layerManager()) {
        const QString &globalLayer = DrawingSettingsCommand::s_centerLineLayer;
        if (!globalLayer.isEmpty())
            layer = ctx->layerManager()->getLayer(globalLayer);
        if (!layer)
            layer = ctx->layerManager()->getCurrentLayer();
    }

    QPen pen;
    if (layer) {
        pen = QPen(layer->color());
        pen.setWidthF(layer->weight());
        QString ltName = layer->linetype();
        if (ltName != "Continuous" && ltName != "Continous" && ctx->lineTypeManager()) {
            pen.setStyle(Qt::CustomDashLine);
            QVector<qreal> pattern = ctx->lineTypeManager()->getQtDashPattern(ltName);
            if (!pattern.isEmpty())
                pen.setDashPattern(pattern);
            else
                pen.setStyle(Qt::SolidLine);
        }
    }

    auto makeLineItem = [&](const QLineF &line) -> QGraphicsItem * {
        QGraphicsItem *item = ctx->entityFactory()->createLine(line, pen);
        if (!item) return nullptr;
        if (LineItem *li = dynamic_cast<LineItem *>(item))
            if (layer) li->setLayerName(layer->name());
        if (layer) {
            QGraphicsItem *folder = m_scene->getLayerRoot(layer->name());
            item->setParentItem(folder);
            if (item->scene() == m_scene)
                m_scene->removeItem(item);
        }
        return item;
    };

    double cx  = center.x(), cy = center.y();
    double ext = qMax(2.0, radius * 0.1);
    QGraphicsItem *hLine = makeLineItem(QLineF(cx - radius - ext, cy, cx + radius + ext, cy));
    QGraphicsItem *vLine = makeLineItem(QLineF(cx, cy - radius - ext, cx, cy + radius + ext));

    if (ctx->undoStack()) {
        ctx->undoStack()->beginMacro(tr("원 중심선"));
        if (hLine) ctx->undoStack()->push(new AddEntityCommand(m_scene, hLine));
        if (vLine) ctx->undoStack()->push(new AddEntityCommand(m_scene, vLine));
        ctx->undoStack()->endMacro();
    } else {
        if (hLine) m_scene->addItem(hLine);
        if (vLine) m_scene->addItem(vLine);
    }

    if (ctx->commandDock())
        ctx->commandDock()->logMessage(tr("원 중심선 생성 완료."), CommandDock::Info);
}

// ── 새 원 + 중심선 생성 ────────────────────────────────────────────────────────

void CircleCenterTool::finishCircleAndCenter(double radius) {
    if (!m_scene || radius < 0.001) {
        cancelRadiusDrawing();
        updatePrompt();
        return;
    }

    IAppContext *ctx = getContext();
    if (!ctx || !ctx->entityFactory() || !ctx->layerManager() || !ctx->lineTypeManager()) {
        cancelRadiusDrawing();
        updatePrompt();
        return;
    }

    // 원 (현재 레이어)
    Layer *curLayer = ctx->layerManager()->getCurrentLayer();
    QPen circlePen(curLayer->color());
    circlePen.setWidthF(curLayer->weight());
    {
        QString lt = curLayer->linetype();
        if (lt == "Continuous" || lt == "Continous") {
            circlePen.setStyle(Qt::SolidLine);
        } else {
            circlePen.setStyle(Qt::CustomDashLine);
            QVector<qreal> pattern = ctx->lineTypeManager()->getQtDashPattern(lt);
            if (!pattern.isEmpty())
                circlePen.setDashPattern(pattern);
            else
                circlePen.setStyle(Qt::SolidLine);
        }
    }

    QGraphicsItem *circleGfx = ctx->entityFactory()->createCircle(m_centerPoint, radius, circlePen);
    if (!circleGfx) {
        cancelRadiusDrawing();
        updatePrompt();
        return;
    }

    if (CadItem *ci = dynamic_cast<CadItem *>(circleGfx))
        ci->setLayerName(curLayer->name());

    {
        QGraphicsItem *folder = m_scene->getLayerRoot(curLayer->name());
        circleGfx->setParentItem(folder);
        if (circleGfx->scene() == m_scene)
            m_scene->removeItem(circleGfx);
    }

    // 중심선 (전역 중심선 레이어)
    Layer *clLayer = nullptr;
    {
        const QString &globalLayer = DrawingSettingsCommand::s_centerLineLayer;
        if (!globalLayer.isEmpty())
            clLayer = ctx->layerManager()->getLayer(globalLayer);
        if (!clLayer)
            clLayer = curLayer;
    }

    QPen clPen(clLayer->color());
    clPen.setWidthF(clLayer->weight());
    {
        QString lt = clLayer->linetype();
        if (lt != "Continuous" && lt != "Continous") {
            clPen.setStyle(Qt::CustomDashLine);
            QVector<qreal> pattern = ctx->lineTypeManager()->getQtDashPattern(lt);
            if (!pattern.isEmpty())
                clPen.setDashPattern(pattern);
            else
                clPen.setStyle(Qt::SolidLine);
        }
    }

    auto makeLineItem = [&](const QLineF &line) -> QGraphicsItem * {
        QGraphicsItem *item = ctx->entityFactory()->createLine(line, clPen);
        if (!item) return nullptr;
        if (LineItem *li = dynamic_cast<LineItem *>(item))
            li->setLayerName(clLayer->name());
        QGraphicsItem *folder = m_scene->getLayerRoot(clLayer->name());
        item->setParentItem(folder);
        if (item->scene() == m_scene)
            m_scene->removeItem(item);
        return item;
    };

    double cx  = m_centerPoint.x(), cy = m_centerPoint.y();
    double ext = qMax(2.0, radius * 0.1);
    QGraphicsItem *hLine = makeLineItem(QLineF(cx - radius - ext, cy, cx + radius + ext, cy));
    QGraphicsItem *vLine = makeLineItem(QLineF(cx, cy - radius - ext, cx, cy + radius + ext));

    if (ctx->undoStack()) {
        ctx->undoStack()->beginMacro(tr("원 중심선 생성"));
        ctx->undoStack()->push(new AddEntityCommand(m_scene, circleGfx));
        if (hLine) ctx->undoStack()->push(new AddEntityCommand(m_scene, hLine));
        if (vLine) ctx->undoStack()->push(new AddEntityCommand(m_scene, vLine));
        ctx->undoStack()->endMacro();
    } else {
        m_scene->addItem(circleGfx);
        if (hLine) m_scene->addItem(hLine);
        if (vLine) m_scene->addItem(vLine);
    }

    if (ctx->commandDock())
        ctx->commandDock()->logMessage(tr("원과 중심선 생성 완료."), CommandDock::Info);

    cancelRadiusDrawing();
    updatePrompt();
}
