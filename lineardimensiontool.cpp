#include "lineardimensiontool.h"
#include "lineardimensionitem.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include <QUndoStack>
#include <QKeyEvent>

LinearDimensionTool::LinearDimensionTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitStartPoint), m_isDrawing(false) {
    m_data = MyDimension();
    m_data.dimensionType = DimensionType::Linear;
}

LinearDimensionTool::~LinearDimensionTool() { cancelDrawing(); }

void LinearDimensionTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    m_state = WaitStartPoint;
    m_isDrawing = false;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("DIMLINEAR", CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(tr(" 첫번째 치수보조선 점을 지정 하세요 : "), {});
    }
}

void LinearDimensionTool::deactivate() {
    cancelDrawing();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(false);
    }
    AbstractTool::deactivate();
}

void LinearDimensionTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;
    IAppContext *ctx = getContext();

    if (button == Qt::RightButton) {
        onCancel();
        return;
    }
    if (button != Qt::LeftButton) return;

    QPointF snappedPos = pos;
    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state != WaitStartPoint) ? m_data.start.toPointF() : QPointF();
        snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    }

    if (m_state == WaitStartPoint) {
        m_data.start = MyVector(snappedPos);
        m_state = WaitEndPoint;
        m_isDrawing = true;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 두번쩨 치수보조선 점을 지정 하세요 : "), {});
        }
    } else if (m_state == WaitEndPoint) {
        m_data.end = MyVector(snappedPos);
        m_data.dimLine = m_data.end;
        m_state = WaitDimLinePoint;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 치수선이 놓일 위치를 지정 하세요 : "), {});
        }
    } else if (m_state == WaitDimLinePoint) {
        m_data.dimLine = MyVector(snappedPos);
        finalizeDimension();
        m_state = WaitStartPoint;
        m_isDrawing = false;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 첫번째 치수보조선 점을 지정 하세요 <종료: 엔터,스페이스>: "), {});
        }
    }
}

void LinearDimensionTool::mouseMove(QPointF pos) {
    m_currentMousePos = pos;
    IAppContext *ctx = getContext();

    QPointF finalPos = pos;
    SnapType snapType = SnapType::None;

    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state != WaitStartPoint) ? m_data.start.toPointF() : QPointF();
        SnapManager::SnapResult result = ctx->snapManager()->snapPoint(pos, m_scene, base);
        finalPos = result.snappedPos;
        snapType = result.type;
    }

    if (m_scene) {
        m_scene->setSnapMarker(finalPos, snapType);
    }

    if (m_isDrawing) {
        if (m_state == WaitEndPoint) {
            m_data.end = MyVector(finalPos);
            m_data.dimLine = m_data.end;
        } else if (m_state == WaitDimLinePoint) {
            m_data.dimLine = MyVector(finalPos);
        }
        updateGhostPreview();
    }
}

void LinearDimensionTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
    Q_UNUSED(pos);
    Q_UNUSED(button);
}

void LinearDimensionTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        onCancel();
        return;
    }
    AbstractTool::keyPress(event);
}

bool LinearDimensionTool::processCommand(const QString &command) {
    Q_UNUSED(command);
    return false;
}

void LinearDimensionTool::onFinish() {
    if (m_state == WaitDimLinePoint) {
        finalizeDimension();
    }
}

void LinearDimensionTool::onCancel() {
    cancelDrawing();
    if (getContext() && getContext()->commandDock()) {
        getContext()->commandDock()->setPrompt(" Command : ");
    }
    emit toolFinished();
}

void LinearDimensionTool::cancelDrawing() {
    m_isDrawing = false;
    m_state = WaitStartPoint;
    m_data = MyDimension();
    m_data.dimensionType = DimensionType::Linear;
    if (m_scene) m_scene->clearOverlay();
}

void LinearDimensionTool::finalizeDimension() {
    if (!m_scene) return;
    if (m_data.start.distanceTo(m_data.end) < 0.001) return;

    IAppContext *ctx = getContext();
    if (ctx && ctx->layerManager()) {
        Layer *curLayer = ctx->layerManager()->getCurrentLayer();
        DimensionStyle style = m_scene->dimensionStyle();

        // 새 치수 생성 시 전역 스타일의 제로 억제 설정을 상속받음
        m_data.precision = style.precision;
        m_data.suppressTrailingZeros = style.suppressTrailingZeros;
        m_data.suppressTrailingZerosTol = style.suppressTrailingZerosTol;

        // [핵심 수정] 스타일에서 지정한 전용 레이어가 있으면 그 레이어를 사용, 없으면 커런트 레이어 사용
        QString targetLayerName = style.dimLineLayer;
        if (targetLayerName.isEmpty() || !ctx->layerManager()->getLayer(targetLayerName)) {
            targetLayerName = curLayer->name();
        }

        Layer *targetLayer = ctx->layerManager()->getLayer(targetLayerName);

        // 펜 속성도 타겟 레이어의 속성을 따라갑니다.
        QPen pen(targetLayer->color());
        pen.setWidthF(targetLayer->weight());

        applyDetailRegionScale(m_data); // 상세도 영역 안이면 measureScale=1/축척
        LinearDimensionItem *item = new LinearDimensionItem(m_data);
        item->setLayerName(targetLayer->name());
        item->setPen(pen);
        item->setGripsVisible(false);

        // [핵심 수정] 커런트 레이어가 아닌, 정확한 전용 레이어 루트에 소속시킵니다.
        QGraphicsItem *layerRoot = m_scene->getLayerRoot(targetLayer->name());
        item->setParentItem(layerRoot);

        if (ctx->undoStack()) {
            ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
        } else {
            m_scene->addItem(item);
        }
    }
    m_scene->clearOverlay();
    m_data = MyDimension();
    m_data.dimensionType = DimensionType::Linear;
}

void LinearDimensionTool::updateGhostPreview() {
    if (!m_isDrawing || !m_scene) return;
    LinearDimensionItem ghost(m_data);
    m_scene->setGhostPath(ghost.geometryPath(), true, Qt::yellow, 1.0);
}