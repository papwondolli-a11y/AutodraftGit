#include "leadertool.h"
#include "appcontext.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "leaderitem.h"
#include "layer.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include "cadutils.h"

#include <QUndoStack>

LeaderTool::LeaderTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitFirstPoint), m_isDrawing(false) {}

void LeaderTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);
    m_state = WaitFirstPoint;
    m_isDrawing = false;
    m_data = MyLeader();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("LEADER", CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(tr(" 시작점을 지정하세요 (arrow head) : "), {});
    }
}

void LeaderTool::deactivate() {
    onCancel();
    AbstractTool::deactivate();
}

void LeaderTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (button == Qt::RightButton) {
        onCancel();
        return;
    }
    if (button != Qt::LeftButton)
        return;

    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    if (ctx && ctx->snapManager()) {
        snapped = ctx->snapManager()->snapPoint(pos, m_scene).snappedPos;
    }

    if (m_state == WaitFirstPoint) {
        m_data.points.append(MyVector(snapped));
        m_state = WaitSecondPoint;
        m_isDrawing = true;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 다음 점을 지정 하세요 : "), {});
        }
    } else if (m_state == WaitSecondPoint) {
        m_data.points.append(MyVector(snapped));
        m_state = WaitText;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 글자를 입력 하세요 ( 종료: 엔터,스페이스) : "), {});
        }
    }
}

void LeaderTool::mouseMove(QPointF pos) {
    m_currentMousePos = pos;
    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    if (ctx && ctx->snapManager() && m_scene) {
        snapped = ctx->snapManager()->snapPoint(pos, m_scene).snappedPos;
    }

    if (m_scene) {
        m_scene->setSnapMarker(snapped, (ctx && ctx->snapManager()) ? ctx->snapManager()->snapPoint(pos, m_scene).type : SnapType::None);
    }

    if (m_isDrawing && m_state == WaitSecondPoint) {
        updateGhostPreview();
    }
}

void LeaderTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
    Q_UNUSED(pos);
    Q_UNUSED(button);
}

bool LeaderTool::processCommand(const QString &command) {
    if (m_state == WaitText) {
        // [수정] 입력받은 텍스트(command)를 CadUtils를 통해 특수문자 변환 후 저장
        m_data.text = CadUtils::processText(command);
        finalizeLeader();
        onFinish();
        return true;
    }
    return false;
}

void LeaderTool::onFinish() {
    m_state = WaitFirstPoint;
    m_isDrawing = false;
    m_data = MyLeader();
    if (m_scene)
        m_scene->clearOverlay();
    emit toolFinished();
}

void LeaderTool::onCancel() {
    m_state = WaitFirstPoint;
    m_isDrawing = false;
    if (m_scene)
        m_scene->clearOverlay();
    emit toolFinished();
}

void LeaderTool::finalizeLeader() {
    if (!m_scene || m_data.points.size() < 2)
        return;

    MyVector last = m_data.points.last();
    MyVector prev = m_data.points[m_data.points.size() - 2];
    double dx = (last.x >= prev.x) ? m_data.landingLength : -m_data.landingLength;
    m_data.points.append(MyVector(last.x + dx, last.y));

    IAppContext *ctx = getContext();
    if (ctx && ctx->layerManager()) {
        QString layerName = m_scene->dimensionStyle().dimLineLayer;
        if (layerName.isEmpty()) {
            layerName = ctx->layerManager()->getCurrentLayer()->name();
        }

        Layer *layer = ctx->layerManager()->getLayer(layerName);
        if (!layer) layer = ctx->layerManager()->getCurrentLayer();

        LeaderItem *item = new LeaderItem(m_data);
        item->setLayerName(layer->name());

        // [핵심] 생성 직후 그립을 명시적으로 숨김
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

void LeaderTool::updateGhostPreview() {
    if (!m_scene || m_data.points.isEmpty())
        return;

    QPainterPath path;
    path.moveTo(m_data.points[0].toPointF());
    for(int i=1; i<m_data.points.size(); ++i) {
        path.lineTo(m_data.points[i].toPointF());
    }

    IAppContext *ctx = getContext();
    QPointF snapped = m_currentMousePos;
    if (ctx && ctx->snapManager()) {
        snapped = ctx->snapManager()->snapPoint(m_currentMousePos, m_scene).snappedPos;
    }
    path.lineTo(snapped);

    AbstractTool::updateGhostPreview(path, currentLayerName());
}