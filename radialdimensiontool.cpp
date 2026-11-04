#include "radialdimensiontool.h"
#include "radialdimensionitem.h"
#include "circleitem.h"
#include "arcitem.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"

#include <QUndoStack>
#include <QKeyEvent>

RadialDimensionTool::RadialDimensionTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitSelectArc), m_isDrawing(false) {
    m_data = MyDimension();
    m_data.dimensionType = DimensionType::Radial;
}

RadialDimensionTool::~RadialDimensionTool() { cancelDrawing(); }

void RadialDimensionTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    m_state = WaitSelectArc;
    m_isDrawing = false;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("DIMRADIAL", CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(tr(" 원 또는 호를 선택 하세요 : "), {});
    }
}

void RadialDimensionTool::deactivate() {
    cancelDrawing();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(false);
    }
    AbstractTool::deactivate();
}

void RadialDimensionTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;
    IAppContext *ctx = getContext();

    if (button == Qt::RightButton) {
        if (m_state == WaitDimLinePoint) {
            // [수량] 마우스 오른쪽 버튼으로 설정값 승인 (확정)
            finalizeDimension();
            m_state = WaitSelectArc;
            m_isDrawing = false;
            if (ctx && ctx->commandDock()) {
                ctx->commandDock()->showInteractivePrompt(tr(" 원 또는 호를 선택 하세요 : "), {});
            }
        } else {
            onCancel();
        }
        return;
    }
    if (button != Qt::LeftButton) return;

    if (m_state == WaitSelectArc) {
        QList<QGraphicsItem *> items = m_scene->items(pos);

        // 인덱스 기반 순환 (C++11 Range-loop 방지)
        for (int i = 0; i < items.size(); ++i) {
            if (CircleItem *circle = dynamic_cast<CircleItem *>(items[i])) {
                m_data.dimensionType = DimensionType::Diametric;
                m_data.isFullCircle = true; // [수정] 원 선택 시 플래그 설정
                m_data.start = circle->getCircle().center;
                m_data.end = circle->getCircle().center + MyVector(circle->getCircle().radius, 0);
                m_state = WaitDimLinePoint;
                m_isDrawing = true;
                if (ctx && ctx->commandDock()) {
                    ctx->commandDock()->showInteractivePrompt(tr(" 치수선이 놓일 위치를 지정 하세요 : "), {});
                }
                return;
            } else if (ArcItem *arc = dynamic_cast<ArcItem *>(items[i])) {
                // [수량] 호 선택 시 반지름(Radial)이 기본이지만 지름(Diametric) 전환 옵션 제공
                m_data.dimensionType = DimensionType::Radial;
                m_data.isFullCircle = false; // [수정] 호 선택 시 플래그 해제
                m_data.start = arc->getArc().center;
                m_data.end = arc->getArc().center + MyVector(arc->getArc().radius, 0);
                m_state = WaitDimLinePoint;
                m_isDrawing = true;
                if (ctx && ctx->commandDock()) {
                    ctx->commandDock()->showInteractivePrompt(tr(" 치수선이 놓일 위치를 지정 하세요  [Radius/Diameter]<Radius> : "), {"r", "d"});
                }
                return;
            }
        }
    } else if (m_state == WaitDimLinePoint) {
        m_data.dimLine = MyVector(pos);
        finalizeDimension();
        m_state = WaitSelectArc;
        m_isDrawing = false;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 원 또는 호를 선택 하세요 : "), {});
        }
    }
}

void RadialDimensionTool::mouseMove(QPointF pos) {
    m_currentMousePos = pos;
    if (m_isDrawing && m_state == WaitDimLinePoint) {
        m_data.dimLine = MyVector(pos);
        updateGhostPreview();
    }
}

void RadialDimensionTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
    Q_UNUSED(pos);
    Q_UNUSED(button);
}

void RadialDimensionTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        onCancel();
        return;
    }
    AbstractTool::keyPress(event);
}

bool RadialDimensionTool::processCommand(const QString &command) {
    QString cmd = command.trimmed().toLower();
    IAppContext *ctx = getContext();

    if (cmd == "r") {
        m_data.dimensionType = DimensionType::Radial;
        if (ctx && ctx->commandDock()) ctx->commandDock()->logMessage(tr("반지름 모드로 설정"), CommandDock::Command);
        updateGhostPreview();
        return true;
    } else if (cmd == "d") {
        m_data.dimensionType = DimensionType::Diametric;
        if (ctx && ctx->commandDock()) ctx->commandDock()->logMessage(tr("지름 모드로 설정"), CommandDock::Command);
        updateGhostPreview();
        return true;
    }

    if (cmd.isEmpty() && m_state == WaitDimLinePoint) {
        // [수량] 스페이스, 엔터로 승인 (빈 명령어)
        finalizeDimension();
        m_state = WaitSelectArc;
        m_isDrawing = false;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 원 또는 호를 선택 하세요 : "), {});
        }
        return true;
    }

    return false;
}

void RadialDimensionTool::onFinish() {
    if (m_state == WaitDimLinePoint) {
        finalizeDimension();
    }
}

void RadialDimensionTool::onCancel() {
    cancelDrawing();
    if (getContext() && getContext()->commandDock()) {
        getContext()->commandDock()->setPrompt(" Command : ");
    }
    emit toolFinished();
}

void RadialDimensionTool::cancelDrawing() {
    m_isDrawing = false;
    m_state = WaitSelectArc;
    m_data = MyDimension();
    m_data.dimensionType = DimensionType::Radial;
    if (m_scene) m_scene->clearOverlay();
}

void RadialDimensionTool::finalizeDimension() {
    if (!m_scene) return;

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
        RadialDimensionItem *item = new RadialDimensionItem(m_data);
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
    m_data.dimensionType = DimensionType::Radial;
}

void RadialDimensionTool::updateGhostPreview() {
    RadialDimensionItem ghost(m_data);
    m_scene->setGhostPath(ghost.geometryPath(), true, Qt::yellow, 1.0);
}