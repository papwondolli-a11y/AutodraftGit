#include "abstractboltplacementtool.h"
#include "boltgeometry.h"
#include "caditem.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "mygraphicsscene.h"
#include "layermanager.h"
#include "layer.h"
#include "snapmanager.h"
#include "drawingsettingscommand.h"

#include <QUndoStack>
#include <QKeyEvent>
#include <QPen>
#include <QtMath>
#include <algorithm>

AbstractBoltPlacementTool::AbstractBoltPlacementTool(QObject *parent)
    : AbstractTool(parent), m_length(0.0), m_twoPointMode(false),
      m_centerlineEnabled(true), m_sideViewEnabled(true), m_state(WaitPosition)
{}

void AbstractBoltPlacementTool::finishActivation(const QString &logMessage) {
    m_state = WaitPosition;
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock() && !logMessage.isEmpty()) {
        ctx->commandDock()->logMessage(logMessage, CommandDock::Command);
    }
    showPositionPrompt();
}

void AbstractBoltPlacementTool::cancelTool() {
    m_state = WaitPosition;
    if (m_scene) m_scene->clearOverlay();
    emit toolFinished();
}

void AbstractBoltPlacementTool::showPositionPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->showInteractivePrompt(positionPromptText(), {});
    }
}

void AbstractBoltPlacementTool::abortCurrentPlacement() {
    m_state = WaitPosition;
    if (m_scene) m_scene->clearOverlay();
    showPositionPrompt();
}

void AbstractBoltPlacementTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape ||
        event->key() == Qt::Key_Return ||
        event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Space) {

        if (m_state == WaitPosition) {
            cancelTool();
        } else { // WaitAngle
            if (event->key() == Qt::Key_Escape) {
                abortCurrentPlacement();
            } else {
                // 숫자 입력 없이 Enter/Space → 각도 0°(기본 방향)으로 확정
                placeItemAndReset(0.0);
            }
        }
        return;
    }
    AbstractTool::keyPress(event);
}

bool AbstractBoltPlacementTool::processCommand(const QString &command) {
    if (m_state == WaitAngle) {
        QString trimmed = command.trimmed();
        if (trimmed.isEmpty()) {
            placeItemAndReset(0.0);
            return true;
        }
        bool ok = false;
        double angle = trimmed.toDouble(&ok);
        if (ok) {
            // 커맨드라인으로 각도를 직접 입력한 경우: 그 각도로 바로 배치
            placeItemAndReset(angle);
            return true;
        }
    }
    return AbstractTool::processCommand(command);
}

void AbstractBoltPlacementTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;

    if (button == Qt::RightButton) {
        if (m_state == WaitPosition) {
            cancelTool();
        } else {
            abortCurrentPlacement();
        }
        return;
    }

    if (button != Qt::LeftButton) return;

    IAppContext *ctx = getContext();
    QPointF snappedPos = pos;
    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state == WaitAngle) ? m_origin : QPointF();
        snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    }

    if (m_state == WaitPosition) {
        m_origin = snappedPos;
        m_state = WaitAngle;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(
                tr(" 방향 각도 지정 또는 숫자 입력 (Enter=0°): "), {});
        }
        return;
    }

    // WaitAngle: 클릭 위치 → 원점 기준 각도로 확정하고 배치
    MyVector dir = MyVector(snappedPos) - MyVector(m_origin);
    double angle = (dir.magnitude() > 1e-6)
        ? qRadiansToDegrees(qAtan2(dir.y, dir.x))
        : 0.0;
    // 2점 지정 모드면 기준점→이 클릭점 사이 거리를 그대로 길이(L)로 쓴다.
    double lengthOverride = m_twoPointMode ? dir.magnitude() : -1.0;
    placeItemAndReset(angle, lengthOverride);
}

void AbstractBoltPlacementTool::mouseMove(QPointF pos) {
    if (!m_scene) return;

    IAppContext *ctx = getContext();
    QPointF finalPos = pos;
    SnapType snapType = SnapType::None;

    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state == WaitAngle) ? m_origin : QPointF();
        SnapManager::SnapResult res = ctx->snapManager()->snapPoint(pos, m_scene, base);
        finalPos = res.snappedPos;
        snapType = res.type;
    }

    m_scene->setSnapMarker(finalPos, snapType);

    if (m_state != WaitAngle) return;

    MyVector dir = MyVector(finalPos) - MyVector(m_origin);
    double angle = (dir.magnitude() > 1e-6)
        ? qRadiansToDegrees(qAtan2(dir.y, dir.x))
        : 0.0;
    // 2점 지정 모드면 미리보기 길이도 실시간으로 클릭 거리를 따라가게 한다.
    double previewLength = m_twoPointMode ? std::max(1.0, dir.magnitude()) : m_length;

    updateGhostLinePreview(QLineF(m_origin, finalPos), currentLayerName());
    QPainterPath path = buildPlacementPreviewPath(previewLength, m_origin, angle);
    updateGhostPreview(path, currentLayerName());
}

void AbstractBoltPlacementTool::placeItemAndReset(double angleDeg, double lengthOverride) {
    IAppContext *ctx = getContext();

    // 외형선/골지름선 두 레이어를 각각 준비해둔다.
    QString outlineLayerName = m_outlineLayerName.isEmpty() ? currentLayerName() : m_outlineLayerName;
    QPen outlinePen = m_outlineLayerName.isEmpty() ? currentLayerPen() : getLayerPen(m_outlineLayerName);
    QGraphicsItem *outlineLayerRoot = (ctx && ctx->layerManager() && m_scene)
        ? m_scene->getLayerRoot(outlineLayerName) : nullptr;

    QString threadLayerName = m_threadLayerName.isEmpty() ? currentLayerName() : m_threadLayerName;
    QPen threadPen = m_threadLayerName.isEmpty() ? currentLayerPen() : getLayerPen(m_threadLayerName);
    QGraphicsItem *threadLayerRoot = (ctx && ctx->layerManager() && m_scene)
        ? m_scene->getLayerRoot(threadLayerName) : nullptr;

    // 중심선 전용 레이어 — 다른 중심선 생성 툴들(circlecentertool 등)과 동일하게, 도면설정에
    // 등록된 전역 중심선 레이어(DrawingSettingsCommand::s_centerLineLayer)를 우선 사용한다.
    QString centerLayerName = DrawingSettingsCommand::s_centerLineLayer.isEmpty()
        ? currentLayerName() : DrawingSettingsCommand::s_centerLineLayer;
    QPen centerPen = DrawingSettingsCommand::s_centerLineLayer.isEmpty()
        ? currentLayerPen() : getLayerPen(DrawingSettingsCommand::s_centerLineLayer);
    QGraphicsItem *centerLayerRoot = (ctx && ctx->layerManager() && m_scene)
        ? m_scene->getLayerRoot(centerLayerName) : nullptr;

    // 2점 지정 모드에서 클릭으로 확정한 경우에만 lengthOverride가 주어진다.
    double effectiveLength = (lengthOverride > 0.0) ? lengthOverride : m_length;
    QList<QGraphicsItem *> items = buildPlacementGeometry(effectiveLength, m_origin, angleDeg);

    if (ctx && ctx->undoStack()) {
        ctx->undoStack()->beginMacro(toolMacroLabel());
        for (int i = 0; i < items.size(); ++i) {
            QGraphicsItem *gi = items.at(i);
            // buildPlacementGeometry()가 만드는 아이템은 항상 LineItem/ArcItem/CircleItem
            // (모두 CadItem)이므로 안전하게 캐스팅 가능
            CadItem *ci = static_cast<CadItem *>(gi);
            bool isCenterline = gi->data(kBoltCenterlineLayerDataKey).toBool();
            bool isPhantom    = gi->data(kBoltPhantomLayerDataKey).toBool();
            bool isThreadDia  = gi->data(kBoltThreadDiaLayerDataKey).toBool();

            QString targetLayerName;
            QPen basePen;
            QGraphicsItem *layerRoot;
            if (isCenterline) {
                targetLayerName = centerLayerName;
                basePen = centerPen;
                layerRoot = centerLayerRoot;
            } else {
                targetLayerName = isThreadDia ? threadLayerName : outlineLayerName;
                basePen = isThreadDia ? threadPen : outlinePen;
                layerRoot = isThreadDia ? threadLayerRoot : outlineLayerRoot;
            }

            ci->setLayerName(targetLayerName);
            if (isCenterline || isPhantom) {
                // 중심선/가상선은 배치되는 레이어에 실제로 설정된 선종류(점선 간격 등)를
                // 그대로 따라야 하므로, buildGeometry가 넣어둔 임시 스타일을 걷어내고
                // 레이어 펜(색상/굵기/선종류)을 통째로 적용한다.
                ci->setPen(basePen);
            } else {
                // 외형선/골지름선은 레이어 펜(색상/굵기/선종류)을 통째로 적용하되,
                // 골지름선만 Lisp 원본 사양대로 굵기를 절반으로 줄인다.
                QPen p = basePen;
                if (isThreadDia) {
                    p.setWidthF(std::max(0.1, basePen.widthF() * 0.5));
                }
                ci->setPen(p);
            }
            if (layerRoot) gi->setParentItem(layerRoot);
            ctx->undoStack()->push(new AddEntityCommand(m_scene, gi));
        }
        ctx->undoStack()->endMacro();
    } else if (m_scene) {
        for (int i = 0; i < items.size(); ++i) {
            m_scene->addItem(items.at(i));
        }
    }

    m_state = WaitPosition;
    if (m_scene) m_scene->clearOverlay();
    showPositionPrompt();
}
