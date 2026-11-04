#include "angulardimensiontoolA.h"
#include "appcontext.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "angulardimensionitem.h" // [수정] DimensionItem 대신 AngularDimensionItem 사용
#include "iappcontext.h"
#include "lineitem.h"
#include "layer.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include "myline.h"
#include "mymath.h"
#include "snapmanager.h"
#include <QtMath>
#include <QUndoStack>

AngularDimensionToolA::AngularDimensionToolA(QObject *parent)
    : AbstractTool(parent), m_state(WaitLine1), m_isDrawing(false) {
    m_data = MyDimension();
    m_data.dimensionType = DimensionType::Angular;
}

AngularDimensionToolA::~AngularDimensionToolA() { cancelDrawing(); }

void AngularDimensionToolA::activate(MyGraphicsScene *scene) {
    if (m_scene == scene)
        return;
    AbstractTool::activate(scene);
    m_state = WaitLine1;
    m_isDrawing = false;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("DIMANGULAR", CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(tr(" 첫번째 라인을 선택 하세요 : "), {});
    }
}

void AngularDimensionToolA::deactivate() {
    cancelDrawing();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(false);
    }
    AbstractTool::deactivate();
}

void AngularDimensionToolA::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene)
        return;
    IAppContext *ctx = getContext();

    if (button == Qt::RightButton) {
        onCancel();
        return;
    }

    if (button != Qt::LeftButton)
        return;

    if (m_state == WaitLine1 || m_state == WaitLine2) {
        LineItem *selectedLine = dynamic_cast<LineItem *>(itemAtPos(pos));

        if (selectedLine) {
            if (m_state == WaitLine1) {
                m_line1 = selectedLine;
                m_line1->setSelected(true);
                m_state = WaitLine2;
                if (ctx && ctx->commandDock()) {
                    ctx->commandDock()->showInteractivePrompt(tr(" 두번째 라인을 선택 하세요 : "), {});
                }
            } else if (m_state == WaitLine2) {
                if (selectedLine == m_line1) return; // Cannot select same line
                m_line2 = selectedLine;
                m_line2->setSelected(true);

                // Calculate vertex and directions
                MyLine l1 = m_line1->getLine();
                MyLine l2 = m_line2->getLine();

                std::vector<MyVector> inters = MyMath::intersectLineLineInfinite(l1, l2);
                if (inters.empty()) {
                    // 평행한 선분인 경우 각도 치수를 기입할 수 없음
                    if (ctx && ctx->commandDock()) {
                        ctx->commandDock()->logMessage(tr("두선이 평행선 입니다."), CommandDock::Error);
                    }
                    cancelDrawing();
                    return;
                }

                m_data.start = inters[0]; // 꼭짓점(Vertex)

                // 꼭짓점 기준 방향 결정을 위해 꼭짓점에서 더 먼 점을 방향점으로 지정
                auto getDirectionPoint = [&](const MyLine &line, const MyVector &vertex) {
                    double d1 = line.start.distanceTo(vertex);
                    double d2 = line.end.distanceTo(vertex);
                    return (d1 > d2) ? line.start : line.end;
                };

                m_data.end = getDirectionPoint(l1, m_data.start);
                m_data.end2 = getDirectionPoint(l2, m_data.start);

                m_state = WaitDimLinePoint;
                m_isDrawing = true;
                if (ctx && ctx->commandDock()) {
                    ctx->commandDock()->showInteractivePrompt(tr(" 치수선이 놓일 위치를 지정 하세요 : "), {});
                }
            }
        }
    } else if (m_state == WaitDimLinePoint) {
        QPointF snappedPos = pos;
        if (ctx && ctx->snapManager()) {
            snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, m_data.start.toPointF()).snappedPos;
        }
        m_data.dimLine = MyVector(snappedPos);
        finalizeDimension();

        if (m_line1) m_line1->setSelected(false);
        if (m_line2) m_line2->setSelected(false);

        m_state = WaitLine1;
        m_isDrawing = false;
        m_line1 = nullptr;
        m_line2 = nullptr;

        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 첫번째 선을 선택 하세요 (종료 : 스페이스)  : "), {});
        }
    }
}

void AngularDimensionToolA::mouseMove(QPointF pos) {
    m_currentMousePos = pos;
    IAppContext *ctx = getContext();

    QPointF finalPos = pos;
    SnapType snapType = SnapType::None;

    if (ctx && ctx->snapManager()) {
        SnapManager::SnapResult result =
            ctx->snapManager()->snapPoint(pos, m_scene, (m_state == WaitDimLinePoint) ? m_data.start.toPointF() : QPointF());
        finalPos = result.snappedPos;
        snapType = result.type;
    }

    if (m_scene) {
        m_scene->setSnapMarker(finalPos, snapType);
    }

    if (m_isDrawing && m_state == WaitDimLinePoint) {
        m_data.dimLine = MyVector(finalPos);
        updateGhostPreview();
    }
}

void AngularDimensionToolA::mouseRelease(QPointF pos, Qt::MouseButton button) {
    Q_UNUSED(pos);
    Q_UNUSED(button);
}

bool AngularDimensionToolA::processCommand(const QString &command) {
    Q_UNUSED(command);
    return false;
}

void AngularDimensionToolA::onFinish() {
    if (m_state == WaitDimLinePoint) finalizeDimension();
}

void AngularDimensionToolA::onCancel() {
    cancelDrawing();
    if (getContext() && getContext()->commandDock()) {
        getContext()->commandDock()->setPrompt(" Command : ");
    }
    emit toolFinished();
}

void AngularDimensionToolA::cancelDrawing() {
    if (m_line1) m_line1->setSelected(false);
    if (m_line2) m_line2->setSelected(false);
    m_isDrawing = false;
    m_state = WaitLine1;
    m_line1 = nullptr;
    m_line2 = nullptr;
    m_data = MyDimension();
    m_data.dimensionType = DimensionType::Angular;
    if (m_scene) {
        m_scene->clearOverlay();
    }
}

void AngularDimensionToolA::finalizeDimension() {
    if (!m_scene)
        return;

    IAppContext *ctx = getContext();
    if (ctx && ctx->layerManager()) {
        Layer *curLayer = ctx->layerManager()->getCurrentLayer();
        DimensionStyle style = m_scene->dimensionStyle();

        // [핵심 수정] 스타일에서 지정한 전용 레이어가 있으면 그 레이어를 사용, 없으면 커런트 레이어 사용
        QString targetLayerName = style.dimLineLayer;
        if (targetLayerName.isEmpty() || !ctx->layerManager()->getLayer(targetLayerName)) {
            targetLayerName = curLayer->name();
        }

        Layer *targetLayer = ctx->layerManager()->getLayer(targetLayerName);

        // 펜 속성도 타겟 레이어의 속성을 따라갑니다.
        QPen pen(targetLayer->color());
        pen.setWidthF(targetLayer->weight());

        // [수정] Factory 패턴 대신 명시적인 AngularDimensionItem 객체 생성
        applyDetailRegionScale(m_data); // 상세도 영역 안이면 measureScale=1/축척
        AngularDimensionItem *item = new AngularDimensionItem(m_data);
        item->setLayerName(targetLayer->name());
        item->setPen(pen);

        // [수정] 생성 직후 그립을 보이지 않도록 끔
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

void AngularDimensionToolA::updateGhostPreview() {
    if (!m_isDrawing || !m_scene)
        return;

    //  DimensionItem 대신 AngularDimensionItem 사용
    AngularDimensionItem ghost(m_data);
    m_scene->setGhostPath(ghost.geometryPath(), true, Qt::yellow, 1.0);
}