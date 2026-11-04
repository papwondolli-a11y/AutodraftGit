#include "halfdimensiontool.h"
#include "mymath.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "halfdimensionitem.h" // 직접 HalfDimensionItem 참조
#include "iappcontext.h"
#include "layer.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include <QUndoStack>
#include <QKeyEvent>

HalfDimensionTool::HalfDimensionTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitStartPoint), m_isDrawing(false), m_isHvMode(true) {
    m_data = MyDimension();
    m_data.dimensionType = DimensionType::HalfDimension;
}

HalfDimensionTool::~HalfDimensionTool() { cancelDrawing(); }

void HalfDimensionTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    m_state = WaitStartPoint;
    m_isDrawing = false;
    m_isHvMode = true; // [추가] 항상 수평/수직 모드로 시작

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->showInteractivePrompt(tr(" 첫번째 치수보조선 점을 지정 하세요 [Aligned/HV]<HV> : "), {"a", "hv"});
    }
}

void HalfDimensionTool::deactivate() {
    cancelDrawing();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(false);
    }
    AbstractTool::deactivate();
}

void HalfDimensionTool::mousePress(QPointF pos, Qt::MouseButton button) {
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
        m_state = WaitCenterPoint;
        m_isDrawing = true;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 중심점을 지정 하세요 : "), {});
        }
    } else if (m_state == WaitCenterPoint) {
        m_data.end = MyVector(snappedPos);
        m_state = WaitDimLinePoint;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 치수선이 놓일 위치를 지정 하세요 : "), {});
        }
    } else if (m_state == WaitDimLinePoint) {
        m_data.dimLine = MyVector(snappedPos);
        
        if (m_isHvMode) {
            m_data.fixedAngle = MyMath::linearDimAngle(m_data.start, m_data.end, MyVector(snappedPos));
            m_data.useFixedAngle = true;
        } else {
            MyVector v = m_data.end - m_data.start;
            if (v.magnitude() > 1e-6) {
                m_data.fixedAngle = qRadiansToDegrees(qAtan2(v.y, v.x));
                m_data.useFixedAngle = true;
            }
        }

        finalizeDimension();
        m_state = WaitStartPoint;
        m_isDrawing = false;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 첫번째 치수보조선 점을 지정 하세요<종료:엔터,스페이스> : "), {});
        }
    }
}

void HalfDimensionTool::mouseMove(QPointF pos) {
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
        if (m_state == WaitCenterPoint) {
            m_data.end = MyVector(finalPos);
            m_data.dimLine = MyVector(finalPos);
        } else if (m_state == WaitDimLinePoint) {
            m_data.dimLine = MyVector(finalPos);
        }
        updateGhostPreview();
    }
}

void HalfDimensionTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
    Q_UNUSED(pos);
    Q_UNUSED(button);
}

bool HalfDimensionTool::processCommand(const QString &command) {
    QString cmd = command.trimmed().toLower();
    IAppContext *ctx = getContext();

    if (cmd == "a") {
        m_isHvMode = false;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->logMessage(tr(" 기울기 모드 설정 되었습니다"), CommandDock::Command);
        }
        return true;
    } else if (cmd == "hv" || cmd == "h") {
        m_isHvMode = true;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->logMessage(tr("수평/수직 모드로 설정 되었습니다."), CommandDock::Command);
        }
        return true;
    }
    return false;
}

void HalfDimensionTool::onFinish() {
    if (m_state == WaitDimLinePoint) {
        finalizeDimension();
    } else {
        cancelDrawing();
    }
}

void HalfDimensionTool::onCancel() {
    cancelDrawing();
    if (getContext() && getContext()->commandDock()) {
        getContext()->commandDock()->setPrompt(" Command : ");
    }
    emit toolFinished();
}

void HalfDimensionTool::cancelDrawing() {
    m_isDrawing = false;
    m_state = WaitStartPoint;
    m_isHvMode = true; // [추가] 항상 수평/수직 모드로 시작
    m_data = MyDimension();
    m_data.dimensionType = DimensionType::HalfDimension;
    if (m_scene) {
        m_scene->clearOverlay();
    }
}

void HalfDimensionTool::finalizeDimension() {
    if (!m_scene) return;

    IAppContext *ctx = getContext();
    if (ctx && ctx->layerManager()) {

        // 1. Scene에 설정된 치수선 스타일(DimensionStyle)을 가져옵니다.
        DimensionStyle dimStyle = m_scene->dimensionStyle();

        // 2. 치수 스타일에 정의된 치수선 레이어 이름을 가져옵니다.
        QString targetLayerName = dimStyle.dimLineLayer;

        // 3. 치수선 레이어가 비어있거나 레이어 매니저에 존재하지 않으면 현재 레이어로 폴백.
        if (targetLayerName.isEmpty() || !ctx->layerManager()->getLayer(targetLayerName)) {
            Layer *curLayer = ctx->layerManager()->getCurrentLayer();
            if (curLayer) {
                targetLayerName = curLayer->name();
            }
        }

        // 4. 타겟 레이어의 스타일(색상 등)을 Scene에서 가져옵니다.
//        LayerStyle layerStyle = m_scene->getLayerStyle(targetLayerName);

        Layer *targetLayer = ctx->layerManager()->getLayer(targetLayerName);

        QPen pen(targetLayer->color());
        // 필요에 따라 선 가중치 등을 LayerStyle 객체에서 가져와 적용할 수 있습니다.
       pen.setWidthF(targetLayer->weight());
        applyDetailRegionScale(m_data); // 상세도 영역 안이면 measureScale=1/축척
        HalfDimensionItem *item = new HalfDimensionItem(m_data);

        // 5. 치수선 전용 레이어 이름으로 설정합니다. (원인 해결 부분)
        item->setLayerName(targetLayerName);
        item->setPen(pen);
        item->setGripsVisible(false);

        // 6. 타겟 레이어의 Root Item을 부모로 설정합니다. (원인 해결 부분)
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
    m_data.dimensionType = DimensionType::HalfDimension;
}

void HalfDimensionTool::updateGhostPreview() {
    if (!m_isDrawing || !m_scene) return;

    MyVector pStart = m_data.start;
    MyVector currentPos = MyVector(m_currentMousePos);

    if (m_state == WaitCenterPoint) {
        // [수량] 중심 축 선택 중에는 단순한 고무줄 라인만 표시하여 원점 고정 느낌 제공
        QPainterPath path;
        path.moveTo(pStart.toPointF());
        path.lineTo(currentPos.toPointF());
        m_scene->setGhostPath(path, true, Qt::yellow, 1.0);
    } else if (m_state == WaitDimLinePoint) {
        // 치수선 배치 중: 이미 축이 정해졌으므로 완성된 형태 표시
        MyDimension ghostData = m_data;
        if (m_isHvMode) {
            ghostData.fixedAngle = MyMath::linearDimAngle(m_data.start, m_data.end, MyVector(m_currentMousePos));
        } else {
            MyVector v = m_data.end - m_data.start;
            if (v.magnitude() > 1e-6) {
                ghostData.fixedAngle = qRadiansToDegrees(qAtan2(v.y, v.x));
            }
        }
        ghostData.useFixedAngle = true;
        ghostData.hideExt1 = false;
        ghostData.hideExt2 = false;

        HalfDimensionItem ghost(ghostData);
        m_scene->setGhostPath(ghost.geometryPath(), true, Qt::yellow, 1.0);
    }
}