#include "datumtool.h"
#include "gdtitem.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "mygraphicsscene.h"
#include "layermanager.h"
#include "snapmanager.h"
#include <QUndoStack>
#include <QKeyEvent>
#include <QPen>
#include <QInputDialog>

DatumTool::DatumTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitSelectGdt)
{}

void DatumTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);
    m_state = WaitSelectGdt;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("DATUM Command", CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(tr(" 기하공차 프레임 선택 (추출용): "), {});
    }
}

void DatumTool::cancelTool() {
    m_state = WaitSelectGdt;
    if (m_scene) m_scene->clearOverlay();

    // 단순히 상태만 초기화하는 것이 아니라 도구를 완전히 종료합니다.
    emit toolFinished();
}

void DatumTool::keyPress(QKeyEvent *event) {
    // [수정] Esc, Enter, Return, Space 입력 시 도구 완전히 종료
    if (event->key() == Qt::Key_Escape ||
        event->key() == Qt::Key_Return ||
        event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Space) {
        cancelTool();
        return;
    }
    AbstractTool::keyPress(event);
}

void DatumTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;
    IAppContext *ctx = getContext();

    // [수정] 마우스 우클릭 시 도구 완전히 종료
    if (button == Qt::RightButton) {
        cancelTool();
        return;
    }

    if (button != Qt::LeftButton) return;

    if (m_state == WaitSelectGdt) {
        QList<QGraphicsItem*> itemsAtPos = m_scene->items(pos);
        GdtItem *selectedGdt = nullptr;
        for (int i = 0; i < itemsAtPos.size(); ++i) {
            selectedGdt = dynamic_cast<GdtItem*>(itemsAtPos[i]);
            if (selectedGdt) break;
        }

        if (!selectedGdt) {
            if (ctx && ctx->commandDock()) ctx->commandDock()->logMessage(tr("기하공차 박스를 선택해야 합니다."), CommandDock::Error);
            return;
        }

        QStringList datums = selectedGdt->gdtData().datumRefs;
        if (datums.isEmpty()) {
            if (ctx && ctx->commandDock()) ctx->commandDock()->logMessage(tr("선택한 기하공차에 등록된 데이텀이 없습니다."), CommandDock::Error);
            return;
        }

        QString selectedDatum;
        if (datums.size() == 1) {
            selectedDatum = datums[0];
        } else {
            bool ok;
            selectedDatum = QInputDialog::getItem(nullptr, tr("데이텀 추출"), tr("생성할 데이텀을 선택하세요:"), datums, 0, false, &ok);
            if (!ok || selectedDatum.isEmpty()) return;
        }

        m_data.text = selectedDatum;
        m_data.textHeight = m_scene->dimensionStyle().textSize;

        m_state = WaitStartPoint;
        if (ctx && ctx->commandDock()) ctx->commandDock()->showInteractivePrompt(tr(" 데이텀 시작점 (삼각형 위치) 지정: "), {});

    } else if (m_state == WaitStartPoint) {
        QPointF snappedPos = pos;
        if (ctx && ctx->snapManager()) {
            snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, QPointF()).snappedPos;
        }
        m_data.startPoint = MyVector(snappedPos);
        m_state = WaitEndPoint;
        if (ctx && ctx->commandDock()) ctx->commandDock()->showInteractivePrompt(tr(" 데이텀 끝점 (박스 위치) 지정: "), {});

    } else if (m_state == WaitEndPoint) {
        QPointF snappedPos = pos;
        if (ctx && ctx->snapManager()) {
            snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, m_data.startPoint.toPointF()).snappedPos;
        }
        m_data.endPoint = MyVector(snappedPos);
        finalizeDatum();
    }
}

void DatumTool::mouseMove(QPointF pos) {
    if (!m_scene) return;
    IAppContext *ctx = getContext();

    QPointF finalPos = pos;
    SnapType snapType = SnapType::None;

    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state == WaitEndPoint) ? m_data.startPoint.toPointF() : QPointF();
        SnapManager::SnapResult res = ctx->snapManager()->snapPoint(pos, m_scene, base);
        finalPos = res.snappedPos;
        snapType = res.type;
    }

    m_scene->setSnapMarker(finalPos, snapType);

    if (m_state == WaitEndPoint) {
        DatumData tmp = m_data;
        tmp.endPoint = MyVector(finalPos);

        DatumItem ghost(tmp);
        ghost.setOverrideScale(m_scene->drawingScale());
        updateGhostPreview(ghost.geometryPath(), currentLayerName());
    }
}

void DatumTool::finalizeDatum() {
    if (m_data.startPoint.distanceTo(m_data.endPoint) < 1e-6) {
        cancelTool();
        return;
    }

    DatumItem *item = new DatumItem(m_data);

    IAppContext *ctx = getContext();
    QString targetLayerName = currentLayerName();
    QPen itemPen = currentLayerPen();
    QGraphicsItem *layerRoot = nullptr;

    if (ctx && ctx->layerManager() && m_scene) {
        DimensionStyle style = m_scene->dimensionStyle();
        if (!style.dimLineLayer.isEmpty() && ctx->layerManager()->getLayer(style.dimLineLayer)) {
            targetLayerName = style.dimLineLayer;
        }

        Layer *targetLayer = ctx->layerManager()->getLayer(targetLayerName);
        if (targetLayer) {
            itemPen = QPen(targetLayer->color());
            itemPen.setWidthF(targetLayer->weight());
            layerRoot = m_scene->getLayerRoot(targetLayer->name());
        }
    }

    item->setLayerName(targetLayerName);
    item->setPen(itemPen);
    if (layerRoot) item->setParentItem(layerRoot);

    if (ctx && ctx->undoStack()) {
        ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
    } else if (m_scene) {
        m_scene->addItem(item);
    }

    // 데이텀 배치 후에도 툴을 종료하지 않고 연속으로 작업할 수 있도록 프롬프트 초기화 유지
    m_state = WaitSelectGdt;
    if (m_scene) m_scene->clearOverlay();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->showInteractivePrompt(tr(" 기하공차 프레임 선택 (추출용): "), {});
    }
}