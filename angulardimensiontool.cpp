#include "angulardimensiontool.h"
#include "angulardimensionitem.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include <QUndoStack>
#include <QKeyEvent>

AngularDimensionTool::AngularDimensionTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitVertex), m_isDrawing(false) {
    m_data = MyDimension();
    m_data.dimensionType = DimensionType::Angular;
}

AngularDimensionTool::~AngularDimensionTool() { cancelDrawing(); }

void AngularDimensionTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    m_state = WaitVertex;
    m_isDrawing = false;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("DIMANGULAR", CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(tr(" 꼭지점 지정하세요  (종료: 스페이스,엔터,마우스오른쪽 버튼) :"), {});
    }
}

void AngularDimensionTool::deactivate() {
    cancelDrawing();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(false);
    }
    AbstractTool::deactivate();
}

void AngularDimensionTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;
    IAppContext *ctx = getContext();

    if (button == Qt::RightButton) {
        onCancel();
        return;
    }
    if (button != Qt::LeftButton) return;

    QPointF snappedPos = pos;
    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state != WaitVertex) ? m_data.start.toPointF() : QPointF();
        snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    }

    if (m_state == WaitVertex) {
        m_data.start = MyVector(snappedPos);
        m_state = WaitFirstPoint;
        m_isDrawing = true;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 첫번째 측정각의 끝점을 지정하세요 : "), {});
        }
    } else if (m_state == WaitFirstPoint) {
        m_data.end = MyVector(snappedPos);
        m_state = WaitSecondPoint;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 두번째 측정각의 끝점을 지정하세요 : "), {});
        }
    } else if (m_state == WaitSecondPoint) {
        m_data.end2 = MyVector(snappedPos);
        m_data.dimLine = m_data.end2;
        m_state = WaitDimLinePoint;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 치수문자가  놓일 위치를 지정 하세요 : "), {});
        }
    } else if (m_state == WaitDimLinePoint) {
        m_data.dimLine = MyVector(snappedPos);
        finalizeDimension();
        m_state = WaitVertex;
        m_isDrawing = false;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 꼭지점 지정하세요  (종료: 스페이스,엔터,마우스오른쪽 버튼) :"), {});
        }
    }
}

void AngularDimensionTool::mouseMove(QPointF pos) {
    m_currentMousePos = pos;
    IAppContext *ctx = getContext();

    QPointF finalPos = pos;
    SnapType snapType = SnapType::None;

    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state != WaitVertex) ? m_data.start.toPointF() : QPointF();
        SnapManager::SnapResult result = ctx->snapManager()->snapPoint(pos, m_scene, base);
        finalPos = result.snappedPos;
        snapType = result.type;
    }

    if (m_scene) {
        m_scene->setSnapMarker(finalPos, snapType);
    }

    if (m_isDrawing) {
        if (m_state == WaitFirstPoint) {
            m_data.end = MyVector(finalPos);
            m_data.end2 = MyVector(finalPos);
            m_data.dimLine = MyVector(finalPos);
        } else if (m_state == WaitSecondPoint) {
            m_data.end2 = MyVector(finalPos);
            m_data.dimLine = MyVector(finalPos);
        } else if (m_state == WaitDimLinePoint) {
            m_data.dimLine = MyVector(finalPos);
        }
        updateGhostPreview();
    }
}

void AngularDimensionTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
    Q_UNUSED(pos);
    Q_UNUSED(button);
}

void AngularDimensionTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        onCancel();
        return;
    }
    AbstractTool::keyPress(event);
}

bool AngularDimensionTool::processCommand(const QString &command) {
    Q_UNUSED(command);
    return false;
}

void AngularDimensionTool::onFinish() {
    if (m_state == WaitDimLinePoint) {
        finalizeDimension();
    }
}

void AngularDimensionTool::onCancel() {
    cancelDrawing();
    if (getContext() && getContext()->commandDock()) {
        getContext()->commandDock()->setPrompt(" Command : ");
    }
    emit toolFinished();
}

void AngularDimensionTool::cancelDrawing() {
    m_isDrawing = false;
    m_state = WaitVertex;
    m_data = MyDimension();
    m_data.dimensionType = DimensionType::Angular;
    if (m_scene) m_scene->clearOverlay();
}

void AngularDimensionTool::finalizeDimension() {
    if (!m_scene) return;

    IAppContext *ctx = getContext();
    if (ctx && ctx->layerManager()) {
        Layer *curLayer = ctx->layerManager()->getCurrentLayer();
        DimensionStyle style = m_scene->dimensionStyle();

        // 새 치수 생성 시 전역 스타일의 제로 억제 설정을 상속받음
        m_data.precision = style.precision;
        m_data.suppressTrailingZeros = style.suppressTrailingZeros;
        m_data.suppressTrailingZerosTol = style.suppressTrailingZerosTol;

        //  스타일에서 지정한 전용 레이어가 있으면 그 레이어를 사용, 없으면 커런트 레이어 사용
        QString targetLayerName = style.dimLineLayer;
        if (targetLayerName.isEmpty() || !ctx->layerManager()->getLayer(targetLayerName)) {
            targetLayerName = curLayer->name();
        }

        Layer *targetLayer = ctx->layerManager()->getLayer(targetLayerName);

        // 펜 속성도 타겟 레이어의 속성을 따라갑니다.
        QPen pen(targetLayer->color());
        pen.setWidthF(targetLayer->weight());

        applyDetailRegionScale(m_data); // 상세도 영역 안이면 measureScale=1/축척
        AngularDimensionItem *item = new AngularDimensionItem(m_data);
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
    m_data.dimensionType = DimensionType::Angular;
}

void AngularDimensionTool::updateGhostPreview() {
    if (!m_isDrawing || !m_scene) return;

    QPainterPath ghostPath;

    if (m_state == WaitFirstPoint) {
        // 1단계: 꼭짓점(Vertex)에서 첫 번째 끝점(마우스 위치)으로 향하는 가상선 표시
        ghostPath.moveTo(m_data.start.toPointF());
        ghostPath.lineTo(m_data.end.toPointF());
        m_scene->setGhostPath(ghostPath, true, Qt::yellow, 1.5);
    }
    else if (m_state == WaitSecondPoint) {
        // 2단계: 첫 번째 확정된 선을 유지하면서, 꼭짓점에서 두 번째 끝점(마우스 위치)으로 향하는 선 추가 표시
        ghostPath.moveTo(m_data.start.toPointF());
        ghostPath.lineTo(m_data.end.toPointF());

        ghostPath.moveTo(m_data.start.toPointF());
        ghostPath.lineTo(m_data.end2.toPointF());
        m_scene->setGhostPath(ghostPath, true, Qt::yellow, 1.5);
    }
    else if (m_state == WaitDimLinePoint) {
        // 3단계: 두 선이 모두 확정되었으므로 완전한 각도 치수(호와 텍스트 포함) 가상 객체 표시
        AngularDimensionItem ghost(m_data);
        m_scene->setGhostPath(ghost.geometryPath(), true, Qt::yellow, 1.5);
    }
}