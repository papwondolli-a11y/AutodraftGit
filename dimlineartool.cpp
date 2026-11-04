#include "dimlineartool.h"
#include "lineardimensionitem.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include <QUndoStack>
#include <QKeyEvent>
#include <QtMath>
#include "inputparser.h"
#include "mymath.h"


DimLinearTool::DimLinearTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitStartPoint), m_isDrawing(false) {
    m_data = MyDimension();
    m_data.dimensionType = DimensionType::Linear;
    m_data.useFixedAngle = true;
    m_data.fixedAngle = 0.0; // Default Horizontal
}

DimLinearTool::~DimLinearTool() { cancelDrawing(); }

void DimLinearTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    m_state = WaitStartPoint;
    m_isDrawing = false;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("DIMLINEAR_HV", CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(tr(" 첫번째 치수보조선 점을 지정 하세요 :  "), {});
    }
}

void DimLinearTool::deactivate() {
    cancelDrawing();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(false);
    }
    AbstractTool::deactivate();
}

// 치수선 위치가 두 점의 바운딩박스 밖으로 벗어난 정도로 수평/수직 판정.
// 박스 안쪽(가깝게 지정)이면 두 점의 간격이 큰 축을 측정 방향으로 사용.
double DimLinearTool::decideAngle(const QPointF &pos) const {
    return MyMath::linearDimAngle(m_data.start, m_data.end, MyVector(pos));
}

void DimLinearTool::mousePress(QPointF pos, Qt::MouseButton button) {
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
            ctx->commandDock()->showInteractivePrompt(tr(" 두번째 치수보조선 점을 지정 하세요(점입력 또는 거리값 입력) "), {});
        }
    } else if (m_state == WaitEndPoint) {
        m_data.end = MyVector(snappedPos);
        m_data.dimLine = m_data.end;
        m_state = WaitDimLinePoint;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 치수선이 놓일 위치를 지정 하세요 : "), {});
        }
    } else if (m_state == WaitDimLinePoint) {
        // [핵심] 마지막 클릭 좌표로 최종 각도 및 위치 확정
        m_data.useFixedAngle = true;
        m_data.fixedAngle = decideAngle(snappedPos);

        m_data.dimLine = MyVector(snappedPos);
        finalizeDimension();
        
        m_state = WaitStartPoint;
        m_isDrawing = false;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 첫번째 치수보조선 점을 지정 하세요 : "), {});
        }
    }
}

void DimLinearTool::mouseMove(QPointF pos) {
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
            // 미리보기에서는 일단 선분 각도를 따르거나 기본값 유지
            m_data.useFixedAngle = false; 
            m_data.dimLine = m_data.end;
        } else if (m_state == WaitDimLinePoint) {
            m_data.useFixedAngle = true;
            // [핵심] 마우스 위치에 따라 수평/수직 판단
            m_data.fixedAngle = decideAngle(finalPos);
            m_data.dimLine = MyVector(finalPos);
        }
        updateGhostPreview();
    }
}

void DimLinearTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
    Q_UNUSED(pos);
    Q_UNUSED(button);
}

void DimLinearTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        onCancel();
        return;
    }
    AbstractTool::keyPress(event);
}

bool DimLinearTool::processCommand(const QString &command) {
    QString cmd = command.toLower().trimmed();
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->parser()) return false;

    // (1) 취소/종료 명령
    if (cmd == "exit" || cmd == "cancel") {
        onCancel();
        return true;
    }

    // (2) 좌표 및 거리 입력 처리 (Direct Distance Entry 포함)
    QPointF base = (m_state != WaitStartPoint) ? m_data.start.toPointF() : QPointF(0, 0);
    QPointF resultPos;
    bool isOrtho = (ctx->snapManager() && ctx->snapManager()->isOrtho());

    if (ctx->parser()->parseCoordinate(cmd, base, m_currentMousePos, isOrtho, resultPos)) {
        mousePress(resultPos, Qt::LeftButton);
        return true;
    }

    return false;
}

void DimLinearTool::onFinish() {
    if (m_state == WaitDimLinePoint) {
        finalizeDimension();
    }
}

void DimLinearTool::onCancel() {
    cancelDrawing();
    if (getContext() && getContext()->commandDock()) {
        getContext()->commandDock()->setPrompt(" Command : ");
    }
    emit toolFinished();
}

void DimLinearTool::cancelDrawing() {
    m_isDrawing = false;
    m_state = WaitStartPoint;
    m_data = MyDimension();
    m_data.dimensionType = DimensionType::Linear;
    if (m_scene) m_scene->clearOverlay();
}

void DimLinearTool::finalizeDimension() {
    if (!m_scene) return;
    if (m_data.start.distanceTo(m_data.end) < 0.001) return;

    IAppContext *ctx = getContext();
    if (ctx && ctx->layerManager()) {
        Layer *curLayer = ctx->layerManager()->getCurrentLayer();
        DimensionStyle style = m_scene->dimensionStyle();

        QString targetLayerName = style.dimLineLayer;
        if (targetLayerName.isEmpty() || !ctx->layerManager()->getLayer(targetLayerName)) {
            targetLayerName = curLayer->name();
        }

        Layer *targetLayer = ctx->layerManager()->getLayer(targetLayerName);
        QPen pen(targetLayer->color());
        pen.setWidthF(targetLayer->weight());

        applyDetailRegionScale(m_data); // 상세도 영역 안이면 measureScale=1/축척
        LinearDimensionItem *item = new LinearDimensionItem(m_data);
        item->setLayerName(targetLayer->name());
        item->setPen(pen);
        item->setGripsVisible(false);

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

void DimLinearTool::updateGhostPreview() {
    if (!m_isDrawing || !m_scene) return;
    LinearDimensionItem ghost(m_data);
    m_scene->setGhostPath(ghost.geometryPath(), true, Qt::yellow, 1.0);
}
