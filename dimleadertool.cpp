#include "dimleadertool.h"
#include "appcontext.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include <QUndoStack>
#include <QLineF>

DimLeaderTool::DimLeaderTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitSelectDimension), m_isDrawing(false) {}

void DimLeaderTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);
    m_state = WaitSelectDimension;
    m_isDrawing = false;
    m_selectedDim = nullptr;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("DIMLEADER (치수선 지시선 뽑기)"), CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(tr(" 지시선표기 필요한 치수선을 선택 하세요 : "), {});
    }
}

void DimLeaderTool::deactivate() {
    onCancel();
    AbstractTool::deactivate();
}

void DimLeaderTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (button == Qt::RightButton) {
        onCancel();
        return;
    }
    if (button != Qt::LeftButton) return;

    IAppContext *ctx = getContext();

    if (m_state == WaitSelectDimension) {
        QGraphicsItem *item = m_scene->itemAt(pos, QTransform());
        BaseDimensionItem *dim = dynamic_cast<BaseDimensionItem *>(item);
        if (!dim) {
            QList<QGraphicsItem *> items = m_scene->items(QRectF(pos.x() - 5, pos.y() - 5, 10, 10));
            // 인덱스 기반 루프 사용
            for (int i = 0; i < items.size(); ++i) {
                if ((dim = dynamic_cast<BaseDimensionItem *>(items[i]))) break;
            }
        }

        if (dim) {
            m_selectedDim = dim;

            m_state = WaitLeaderEndPoint;
            m_isDrawing = true;
            if (ctx && ctx->commandDock()) {
                ctx->commandDock()->showInteractivePrompt(tr(" 지시선 끝점을 지정 하세요 : "), {});
            }
        }
    } else if (m_state == WaitLeaderEndPoint) {
        QPointF finalPos = pos;
        if (ctx && ctx->snapManager()) {
            finalPos = ctx->snapManager()->snapPoint(pos, m_scene).snappedPos;
        }

        if (m_selectedDim) {
            const QMap<QString, QVariant> props = m_selectedDim->getProperties();

            if (ctx && ctx->undoStack()) {
                ctx->undoStack()->beginMacro(tr("Add Dimension Leader"));

                // [핵심] 해당 치수선의 지시선(Leader) 모드를 참(true)으로 설정
                ctx->undoStack()->push(new ChangePropertyCommand(m_selectedDim, "UseLeader", props.value("UseLeader"), true));
                ctx->undoStack()->push(new ChangePropertyCommand(m_selectedDim, "UseCustomTextPos", props.value("UseCustomTextPos"), true));
                ctx->undoStack()->push(new ChangePropertyCommand(m_selectedDim, "CustomTextPosX", props.value("CustomTextPosX"), finalPos.x()));
                ctx->undoStack()->push(new ChangePropertyCommand(m_selectedDim, "CustomTextPosY", props.value("CustomTextPosY"), finalPos.y()));

                // 요청대로 화살표 숨김 유지
                ctx->undoStack()->push(new ChangePropertyCommand(m_selectedDim, "HideArrow1", props.value("HideArrow1"), true));
                ctx->undoStack()->push(new ChangePropertyCommand(m_selectedDim, "HideArrow2", props.value("HideArrow2"), true));

                ctx->undoStack()->endMacro();
            } else {
                // 하드 적용 (Undo Stack이 없을 때)
                m_selectedDim->setProperty("UseLeader", true);
                m_selectedDim->setProperty("CustomTextPosX", finalPos.x());
                m_selectedDim->setProperty("CustomTextPosY", finalPos.y());
                m_selectedDim->setProperty("UseCustomTextPos", true);
                m_selectedDim->setArrow1Hidden(true);
                m_selectedDim->setArrow2Hidden(true);
            }
        }

        m_selectedDim = nullptr;
        m_state = WaitSelectDimension;
        m_isDrawing = false;
        m_scene->clearOverlay();
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 지시선표기 필요한 치수선을 선택 하세요 : "), {});
        }
    }
}

void DimLeaderTool::mouseMove(QPointF pos) {
    m_currentMousePos = pos;
    IAppContext *ctx = getContext();
    QPointF snapped = pos;

    if (ctx && ctx->snapManager() && m_scene) {
        snapped = ctx->snapManager()->snapPoint(pos, m_scene).snappedPos;
    }

    if (m_scene) {
        m_scene->setSnapMarker(snapped, (ctx && ctx->snapManager()) ? ctx->snapManager()->snapPoint(pos, m_scene).type : SnapType::None);
    }

    if (m_isDrawing && m_state == WaitLeaderEndPoint) {
        updateGhostPreview();
    }
}

void DimLeaderTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
    Q_UNUSED(pos); Q_UNUSED(button);
}

bool DimLeaderTool::processCommand(const QString &command) {
    Q_UNUSED(command);
    return false;
}

void DimLeaderTool::onFinish() {
    m_state = WaitSelectDimension;
    m_isDrawing = false;
    if (m_scene) m_scene->clearOverlay();
    emit toolFinished();
}

void DimLeaderTool::onCancel() {
    m_state = WaitSelectDimension;
    m_isDrawing = false;
    m_selectedDim = nullptr;
    if (m_scene) m_scene->clearOverlay();
    if (getContext() && getContext()->commandDock()) {
        getContext()->commandDock()->setPrompt(" Command : ");
    }
    emit toolFinished();
}

void DimLeaderTool::updateGhostPreview() {
    if (!m_isDrawing || !m_selectedDim || !m_scene) return;

    CadItem *cloned = m_selectedDim->clone();
    BaseDimensionItem *clonedDim = dynamic_cast<BaseDimensionItem*>(cloned);

    QPointF snapped = m_currentMousePos;
    IAppContext *ctx = getContext();
    if (ctx && ctx->snapManager()) {
        snapped = ctx->snapManager()->snapPoint(m_currentMousePos, m_scene).snappedPos;
    }

    if (clonedDim) {
        // 미리보기 객체에도 UseLeader 플래그를 설정하여 렌더링 방식 전환
        clonedDim->setProperty("UseLeader", true);
        clonedDim->setProperty("CustomTextPosX", snapped.x());
        clonedDim->setProperty("CustomTextPosY", snapped.y());
        clonedDim->setProperty("UseCustomTextPos", true);

        clonedDim->setArrow1Hidden(true);
        clonedDim->setArrow2Hidden(true);

        // shape()는 선택 판정용 스트로크 외곽선(폭 1.0)이라 이중선으로 보인다.
        // 실제 형상 경로(geometryPath)를 사용해 단일선으로 미리보기한다.
        QPainterPath ghostPath = clonedDim->geometryPath();
        m_scene->setGhostPath(ghostPath, true, Qt::yellow, 1.0);
    }

    delete cloned;
}