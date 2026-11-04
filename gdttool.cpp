#include "gdttool.h"
#include "gdtdialog.h"
#include "gdtitem.h"
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

GdtTool::GdtTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitStartPoint)
{}

void GdtTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);

    GdtDialog dlg;
    if (scene) {
        GdtData initData;
        initData.textHeight = scene->dimensionStyle().textSize;
        initData.symbol = GdtSymbol::Circularity;
        dlg.setData(initData);
    }

    // [3-3] 이 도면이 기억한 활성 GD&T 프리셋을 다시 선택한다(./data 의 .gdt 라이브러리).
    ToolDefaults gdtDef("Gdt", scene);
    const QString wantPreset = gdtDef.getString("activePreset", QString());
    if (!wantPreset.isEmpty())
        dlg.preselectPreset(wantPreset);

    if (dlg.exec() != QDialog::Accepted) {
        emit toolFinished();
        return;
    }

    m_pendingData = dlg.resultData();

    // 사용한 프리셋 이름을 도면 스코프에 기록(다음에 이 도면에서 열면 자동 선택).
    if (!dlg.activePresetName().isEmpty())
        gdtDef.put("activePreset", dlg.activePresetName());
    m_pts.clear();
    m_state = WaitStartPoint;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->logMessage("GDT Command", CommandDock::Command);
    }

    // 시작 프롬프트 출력
    showStartPrompt();
}

void GdtTool::cancelTool() {
    m_pts.clear();
    m_state = WaitStartPoint;
    if (m_scene) m_scene->clearOverlay();
    emit toolFinished();
}

// ─────────────────────────────────────────────
// 프롬프트 텍스트 출력 모듈화
// ─────────────────────────────────────────────
void GdtTool::showStartPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        if (m_pendingData.useLeader) {
            ctx->commandDock()->showInteractivePrompt(tr(" 지시선 시작점 지정: "), {});
        } else {
            ctx->commandDock()->showInteractivePrompt(tr(" 공차 프레임을 배치할 위치를 클릭하세요: "), {});
        }
    }
}

// ─────────────────────────────────────────────
// 현재 작업만 취소하고 대기 상태로 복귀
// ─────────────────────────────────────────────
void GdtTool::abortCurrentPlacement() {
    m_pts.clear();
    m_state = WaitStartPoint;
    if (m_scene) m_scene->clearOverlay();
    showStartPrompt();
}

void GdtTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape ||
        event->key() == Qt::Key_Return ||
        event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Space) {

        if (m_state == WaitStartPoint) {
            // 시작점 대기 중일 때 누르면 도구 완전 종료
            cancelTool();
        } else {
            // 그리는 도중일 때
            if (event->key() == Qt::Key_Escape) {
                // ESC는 무조건 현재 그리는 것만 취소
                abortCurrentPlacement();
            } else {
                // Enter, Space는 그리기 완료 시도 (점 개수 부족 시 취소)
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

void GdtTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;

    if (button == Qt::RightButton) {
        if (m_state == WaitStartPoint) {
            // 시작점 대기 중일 때 우클릭하면 도구 완전 종료
            cancelTool();
        } else {
            // 그리는 도중일 때 우클릭하면 그리기 완료 시도
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
        QPointF base = m_pts.isEmpty() ? QPointF() : m_pts.last().toPointF();
        snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    }

    // 지시선 체크박스 비활성화 시 단일 프레임 즉시 배치
    if (!m_pendingData.useLeader) {
        m_pendingData.position = MyVector(snappedPos);
        m_pendingData.rotation = 0.0;
        m_pendingData.leaderPoints.clear();
        placeItemAndReset();
        return;
    }

    // 기존 지시선 3점 배치 로직
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

void GdtTool::mouseMove(QPointF pos) {
    if (!m_scene) return;

    IAppContext *ctx = getContext();
    QPointF finalPos = pos;
    SnapType snapType = SnapType::None;

    if (ctx && ctx->snapManager()) {
        QPointF base = m_pts.isEmpty() ? QPointF() : m_pts.last().toPointF();
        SnapManager::SnapResult res = ctx->snapManager()->snapPoint(pos, m_scene, base);
        finalPos = res.snappedPos;
        snapType = res.type;
    }

    m_scene->setSnapMarker(finalPos, snapType);

    // 지시선 미사용 시 단일 프레임 고스트 렌더링
    if (!m_pendingData.useLeader) {
        GdtData tmp = m_pendingData;
        tmp.position = MyVector(finalPos);
        tmp.rotation = 0.0;
        tmp.leaderPoints.clear();
        GdtItem tmpItem(tmp);
        tmpItem.setOverrideScale(m_scene->drawingScale());
        updateGhostPreview(tmpItem.geometryPath(), currentLayerName());
        return;
    }

    if (m_state == WaitStartPoint) return;

    GdtData tmp = m_pendingData;
    tmp.leaderPoints = m_pts;
    tmp.leaderPoints.append(MyVector(finalPos));
    tmp.position = MyVector(finalPos);

    MyVector pPrev = m_pts.last();
    MyVector dir = MyVector(finalPos) - pPrev;
    tmp.rotation = qRadiansToDegrees(qAtan2(dir.y, dir.x));

    GdtItem tmpItem(tmp);
    tmpItem.setOverrideScale(m_scene->drawingScale());
    updateGhostPreview(tmpItem.geometryPath(), currentLayerName());
}

void GdtTool::finishTool() {
    if (m_pendingData.useLeader) {
        if (m_pts.size() < 2) {
            // [수정] 도구를 끄는 대신 현재 작업만 초기화하고 대기
            abortCurrentPlacement();
            return;
        }
        m_pendingData.leaderPoints = m_pts;
        m_pendingData.position = m_pts.last();

        MyVector pPrev = m_pts[m_pts.size() - 2];
        MyVector pLast = m_pts.last();
        MyVector dir = pLast - pPrev;
        m_pendingData.rotation = qRadiansToDegrees(qAtan2(dir.y, dir.x));
    }

    placeItemAndReset();
}

void GdtTool::placeItemAndReset() {
    GdtItem *item = new GdtItem(m_pendingData);

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

    // 도구 완료 후 다시 대기 상태로 초기화
    m_pts.clear();
    m_state = WaitStartPoint;
    if (m_scene) m_scene->clearOverlay();

    // 시작 프롬프트 재호출
    showStartPrompt();
}