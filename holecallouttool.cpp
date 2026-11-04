#include "holecallouttool.h"
#include "holeitem.h"
#include "holecalloutformatter.h"
#include "holecalloutlink.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "leaderitem.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"

#include <QKeyEvent>
#include <QPainterPath>
#include <QUndoStack>

HoleCalloutTool::HoleCalloutTool(QObject *parent) : AbstractTool(parent) {}

void HoleCalloutTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("HOLECALLOUT (구멍 콜아웃)"), CommandDock::Command);
    }

    resetToSelect();
}

void HoleCalloutTool::deactivate() {
    if (m_scene) {
        m_scene->clearOverlay();
        m_scene->clearSelection();
    }
    AbstractTool::deactivate();
}

void HoleCalloutTool::resetToSelect() {
    m_state = WaitSelectHole;
    m_pickedItems.clear();
    m_selectedHoles.clear();
    m_data = MyLeader();
    if (m_scene) {
        m_scene->clearOverlay();
        m_scene->clearSelection();
    }
    showPrompt();
}

void HoleCalloutTool::showPrompt() {
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;

    switch (m_state) {
    case WaitSelectHole:
        ctx->commandDock()->showInteractivePrompt(
            tr(" 콜아웃을 표기할 구멍을 선택 하세요 (완료: 우클릭/엔터) : "), {});
        break;
    case WaitFirstPoint:
        ctx->commandDock()->showInteractivePrompt(tr(" 지시선 시작점을 지정 하세요 : "), {});
        break;
    case WaitSecondPoint:
        ctx->commandDock()->showInteractivePrompt(tr(" 지시선 끝점(문자 위치)을 지정 하세요 : "), {});
        break;
    }
}

void HoleCalloutTool::cancelTool() {
    if (m_scene) {
        m_scene->clearOverlay();
        m_scene->clearSelection();
    }
    emit toolFinished();
}

bool HoleCalloutTool::pickHole(QPointF pos, HoleItem *&outItem) {
    if (!m_scene) return false;

    QGraphicsItem *item = m_scene->itemAt(pos, QTransform());
    HoleItem *hole = dynamic_cast<HoleItem *>(item);
    if (!hole) {
        const QList<QGraphicsItem *> items = m_scene->items(QRectF(pos.x() - 5, pos.y() - 5, 10, 10));
        for (int i = 0; i < items.size(); ++i) {
            if ((hole = dynamic_cast<HoleItem *>(items[i]))) break;
        }
    }
    if (!hole) return false;

    outItem = hole;
    return true;
}

void HoleCalloutTool::finishSelection() {
    if (m_pickedItems.isEmpty()) {
        cancelTool();
        return;
    }

    m_selectedHoles.clear();
    for (HoleItem *item : std::as_const(m_pickedItems)) {
        m_selectedHoles.append(item->holeData());
    }

    m_state = WaitFirstPoint;
    showPrompt();
}

void HoleCalloutTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        cancelTool();
        return;
    }
    if (m_state == WaitSelectHole &&
        (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space)) {
        finishSelection();
        return;
    }
    AbstractTool::keyPress(event);
}

void HoleCalloutTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;

    if (m_state == WaitSelectHole) {
        if (button == Qt::RightButton) {
            finishSelection();
            return;
        }
        if (button != Qt::LeftButton) return;

        HoleItem *hole = nullptr;
        if (pickHole(pos, hole) && !m_pickedItems.contains(hole)) {
            m_pickedItems.append(hole);
            hole->setSelected(true);
        }
        return;
    }

    if (button == Qt::RightButton) {
        cancelTool();
        return;
    }
    if (button != Qt::LeftButton) return;

    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    if (ctx && ctx->snapManager()) {
        snapped = ctx->snapManager()->snapPoint(pos, m_scene).snappedPos;
    }

    if (m_state == WaitFirstPoint) {
        m_data.points.append(MyVector(snapped));
        m_state = WaitSecondPoint;
        showPrompt();
    } else if (m_state == WaitSecondPoint) {
        m_data.points.append(MyVector(snapped));
        finalizeLeader();
    }
}

void HoleCalloutTool::mouseMove(QPointF pos) {
    if (!m_scene) return;
    m_currentMousePos = pos;

    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    SnapType snapType = SnapType::None;
    if (ctx && ctx->snapManager()) {
        SnapManager::SnapResult res = ctx->snapManager()->snapPoint(pos, m_scene);
        snapped = res.snappedPos;
        snapType = res.type;
    }
    m_scene->setSnapMarker(snapped, snapType);

    if (m_state == WaitSecondPoint) updateLeaderGhost();
}

void HoleCalloutTool::updateLeaderGhost() {
    if (!m_scene || m_data.points.isEmpty()) return;

    QPainterPath path;
    path.moveTo(m_data.points[0].toPointF());
    for (int i = 1; i < m_data.points.size(); ++i) {
        path.lineTo(m_data.points[i].toPointF());
    }

    IAppContext *ctx = getContext();
    QPointF snapped = m_currentMousePos;
    if (ctx && ctx->snapManager()) {
        snapped = ctx->snapManager()->snapPoint(m_currentMousePos, m_scene).snappedPos;
    }
    path.lineTo(snapped);

    updateGhostPreview(path, currentLayerName());
}

// LeaderTool::finalizeLeader와 동일한 방식으로 착지선(landing segment)을 붙이되,
// 문자는 사용자가 입력하는 대신 선택된 구멍(들)의 스펙에서 자동 조립.
void HoleCalloutTool::finalizeLeader() {
    if (!m_scene || m_data.points.size() < 2) return;

    IAppContext *ctx = getContext();

    bool ok = false;
    QString text = HoleCalloutFormatter::formatGrouped(m_selectedHoles, ok);
    if (!ok) {
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(tr("선택된 구멍들의 스펙이 서로 달라 콜아웃을 생성할 수 없습니다."), CommandDock::Info);
        resetToSelect();
        return;
    }
    m_data.text = text;

    // 콜아웃 ↔ 구멍 연결 기록 — 이후 콜아웃 편집은 구멍에, 구멍 편집은 콜아웃에 반영된다
    m_data.holeIds.clear();
    for (HoleItem *hole : std::as_const(m_pickedItems)) {
        const QString id = HoleCalloutLink::ensureHoleId(hole);
        if (!m_data.holeIds.contains(id)) m_data.holeIds.append(id);
    }

    MyVector last = m_data.points.last();
    MyVector prev = m_data.points[m_data.points.size() - 2];
    double dx = (last.x >= prev.x) ? m_data.landingLength : -m_data.landingLength;
    m_data.points.append(MyVector(last.x + dx, last.y));

    if (ctx && ctx->layerManager() && m_scene) {
        QString layerName = m_scene->dimensionStyle().dimLineLayer;
        if (layerName.isEmpty() && ctx->layerManager()->getCurrentLayer()) {
            layerName = ctx->layerManager()->getCurrentLayer()->name();
        }

        Layer *layer = ctx->layerManager()->getLayer(layerName);
        if (!layer) layer = ctx->layerManager()->getCurrentLayer();

        if (layer) {
            LeaderItem *item = new LeaderItem(m_data);
            item->setLayerName(layer->name());
            item->setGripsVisible(false);

            QPen p(layer->color());
            p.setWidthF(layer->weight());
            item->setPen(p);

            QGraphicsItem *layerRoot = m_scene->getLayerRoot(layer->name());
            item->setParentItem(layerRoot);

            if (ctx->undoStack()) {
                ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
            } else {
                m_scene->addItem(item);
            }
        }
    }

    // 연속 배치: 다시 구멍 선택 단계로 복귀 (ESC/빈 선택 우클릭으로 도구 종료)
    resetToSelect();
}
