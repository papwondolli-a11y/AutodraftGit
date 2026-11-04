#include "surfacefinishtool.h"
#include "surfacefinishdialog.h"
#include "surfacefinishitem.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "mygraphicsscene.h"
#include "layermanager.h"
#include "snapmanager.h"
#include "tooldefaults.h"

#include <QUndoStack>
#include <QKeyEvent>
#include <QPen>
#include <QtMath>

// 3-2: 도구 기본값은 ToolDefaults(도면 스코프 우선). 네임스페이스 "SurfaceFinishTool".

SurfaceFinishTool::SurfaceFinishTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitStartPoint)
{}

void SurfaceFinishTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);

    // 꼬리선/지시선/레이어 선택은 마지막으로 사용한 상태를 다음 실행에도 유지한다.
    ToolDefaults sd("SurfaceFinishTool", scene);
    bool savedTailBar = sd.getBool("ShowTailBar", false);
    bool savedUseLeader = sd.getBool("UseLeader", true);
    QString savedSymbolLayer = sd.getString("SymbolLayer", "");
    QString savedTextLayer = sd.getString("TextLayer", "");

    SurfaceFinishDialog dlg(getContext());
    SurfaceFinishData initData;
    initData.showTailBar = savedTailBar;
    initData.useLeader = savedUseLeader;
    initData.symbolLayer = savedSymbolLayer;
    initData.textLayer = savedTextLayer;
    if (scene) {
        initData.textHeight = scene->dimensionStyle().textSize;
    }
    dlg.setData(initData);

    if (dlg.exec() != QDialog::Accepted) {
        emit toolFinished();
        return;
    }

    m_pendingData = dlg.resultData();

    sd.put("ShowTailBar", m_pendingData.showTailBar);
    sd.put("UseLeader", m_pendingData.useLeader);
    sd.put("SymbolLayer", m_pendingData.symbolLayer);
    sd.put("TextLayer", m_pendingData.textLayer);

    m_pts.clear();
    m_state = WaitStartPoint;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->logMessage(tr("표면 거칠기 명령"), CommandDock::Command);
    }

    showStartPrompt();
}

void SurfaceFinishTool::cancelTool() {
    m_pts.clear();
    m_state = WaitStartPoint;
    if (m_scene) m_scene->clearOverlay();
    emit toolFinished();
}

void SurfaceFinishTool::showStartPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        if (m_pendingData.useLeader) {
            ctx->commandDock()->showInteractivePrompt(tr(" 지시선 시작점 지정: "), {});
        } else {
            ctx->commandDock()->showInteractivePrompt(tr(" 기호를 배치할 위치를 클릭하세요: "), {});
        }
    }
}

void SurfaceFinishTool::abortCurrentPlacement() {
    m_pts.clear();
    m_state = WaitStartPoint;
    if (m_scene) m_scene->clearOverlay();
    showStartPrompt();
}

void SurfaceFinishTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape ||
        event->key() == Qt::Key_Return ||
        event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Space) {

        if (m_state == WaitStartPoint) {
            cancelTool();
        } else if (m_state == WaitAngle) {
            if (event->key() == Qt::Key_Escape) {
                abortCurrentPlacement();
            } else {
                // 숫자 입력 없이 Enter/Space → 각도 0(기본값)으로 확정
                finishNoLeaderPlacement(0.0);
            }
        } else {
            if (event->key() == Qt::Key_Escape) {
                abortCurrentPlacement();
            } else {
                if (m_pendingData.useLeader && m_pts.size() >= 2) {
                    finishTool();
                } else {
                    abortCurrentPlacement();
                }
            }
        }
        return;
    }
    AbstractTool::keyPress(event);
}

bool SurfaceFinishTool::processCommand(const QString &command) {
    if (m_state == WaitAngle) {
        QString trimmed = command.trimmed();
        if (trimmed.isEmpty()) {
            finishNoLeaderPlacement(0.0);
            return true;
        }
        bool ok = false;
        double angle = trimmed.toDouble(&ok);
        if (ok) {
            finishNoLeaderPlacement(angle);
            return true;
        }
    }
    return AbstractTool::processCommand(command);
}

void SurfaceFinishTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;

    if (button == Qt::RightButton) {
        if (m_state == WaitStartPoint) {
            cancelTool();
        } else if (m_state == WaitAngle) {
            // 우클릭 → 각도 0(기본값)으로 확정
            finishNoLeaderPlacement(0.0);
        } else {
            if (m_pendingData.useLeader && m_pts.size() >= 2) {
                finishTool();
            } else {
                abortCurrentPlacement();
            }
        }
        return;
    }

    if (button != Qt::LeftButton) return;

    IAppContext *ctx = getContext();
    QPointF snappedPos = pos;
    if (ctx && ctx->snapManager()) {
        // 각도 지정 단계(WaitAngle)에서는 배치 위치가 직교(Ortho) 기준점이어야 한다.
        QPointF base = (m_state == WaitAngle) ? m_pendingData.position.toPointF()
                      : (m_pts.isEmpty() ? QPointF() : m_pts.last().toPointF());
        snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    }

    if (!m_pendingData.useLeader) {
        if (m_state == WaitAngle) {
            MyVector dir = MyVector(snappedPos) - m_pendingData.position;
            double angle = (dir.magnitude() > 1e-6)
                ? qRadiansToDegrees(qAtan2(dir.y, dir.x))
                : 0.0;
            finishNoLeaderPlacement(angle);
            return;
        }

        m_pendingData.position = MyVector(snappedPos);
        m_pendingData.leaderPoints.clear();
        m_state = WaitAngle;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(
                tr(" 각도 지정 또는 숫자 입력 (Enter=0°): "), {});
        }
        return;
    }

    m_pts.append(MyVector(snappedPos));

    if (m_state == WaitStartPoint) {
        m_state = WaitNextPoint;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 다음 점 지정 (완료하려면 우클릭/엔터/스페이스): "), {});
        }
    } else {
        if (m_pts.size() == 3) {
            finishTool();
        }
    }
}

void SurfaceFinishTool::mouseMove(QPointF pos) {
    if (!m_scene) return;

    IAppContext *ctx = getContext();
    QPointF finalPos = pos;
    SnapType snapType = SnapType::None;

    if (ctx && ctx->snapManager()) {
        // 각도 지정 단계(WaitAngle)에서는 배치 위치가 직교(Ortho) 기준점이어야 한다.
        QPointF base = (m_state == WaitAngle) ? m_pendingData.position.toPointF()
                      : (m_pts.isEmpty() ? QPointF() : m_pts.last().toPointF());
        SnapManager::SnapResult res = ctx->snapManager()->snapPoint(pos, m_scene, base);
        finalPos = res.snappedPos;
        snapType = res.type;
    }

    m_scene->setSnapMarker(finalPos, snapType);

    if (!m_pendingData.useLeader) {
        SurfaceFinishData tmp = m_pendingData;
        tmp.leaderPoints.clear();

        if (m_state == WaitAngle) {
            // 기준점→현재 마우스 방향으로 각도를 미리 보여준다
            MyVector dir = MyVector(finalPos) - m_pendingData.position;
            tmp.rotation = (dir.magnitude() > 1e-6)
                ? qRadiansToDegrees(qAtan2(dir.y, dir.x))
                : 0.0;
            updateGhostLinePreview(QLineF(m_pendingData.position.toPointF(), finalPos), currentLayerName());
        } else {
            tmp.position = MyVector(finalPos);
            tmp.rotation = 0.0;
        }

        SurfaceFinishItem tmpItem(tmp);
        tmpItem.setOverrideScale(m_scene->drawingScale());
        tmpItem.setOverrideArrowSize(m_scene->dimensionStyle().arrowSize);
        // shape()는 클릭 판정용으로 두꺼운 리본 형태라 실제 모양과 달라 보이므로,
        // 실제 그려지는 순수 선 형태인 geometryPath()를 미리보기에 사용한다.
        updateGhostPreview(tmpItem.geometryPath(), currentLayerName());
        return;
    }

    if (m_state == WaitStartPoint) return;

    SurfaceFinishData tmp = m_pendingData;
    tmp.leaderPoints = m_pts;
    tmp.leaderPoints.append(MyVector(finalPos));
    tmp.position = MyVector(finalPos);
    // 기호/문자는 항상 정자세로 표시 (지시선 각도를 따라 회전하지 않음)
    tmp.rotation = 0.0;

    SurfaceFinishItem tmpItem(tmp);
    tmpItem.setOverrideScale(m_scene->drawingScale());
    tmpItem.setOverrideArrowSize(m_scene->dimensionStyle().arrowSize);
    updateGhostPreview(tmpItem.geometryPath(), currentLayerName());
}

void SurfaceFinishTool::finishTool() {
    if (m_pendingData.useLeader) {
        if (m_pts.size() < 2) {
            abortCurrentPlacement();
            return;
        }
        m_pendingData.leaderPoints = m_pts;
        m_pendingData.position = m_pts.last();
        // 기호/문자는 항상 정자세로 표시 (지시선 각도를 따라 회전하지 않음)
        m_pendingData.rotation = 0.0;
    }

    placeItemAndReset();
}

void SurfaceFinishTool::finishNoLeaderPlacement(double angle) {
    m_pendingData.rotation = angle;
    placeItemAndReset();
}

void SurfaceFinishTool::placeItemAndReset() {
    SurfaceFinishItem *item = new SurfaceFinishItem(m_pendingData);

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

    m_pts.clear();
    m_state = WaitStartPoint;
    if (m_scene) m_scene->clearOverlay();

    showStartPrompt();
}
