#include "hatchtool.h"
#include "caditem.h"
#include "hatchitem.h"
#include "mygraphicsscene.h"
#include "iappcontext.h"
#include "commanddock.h"
#include "cadcommands.h"
#include "advancedboundaryfinder.h"
#include "hatchdialog.h"
#include "layermanager.h"
#include "snapmanager.h"
#include "tooldefaults.h"
#include <QUndoStack>

HatchTool::HatchTool(QObject *parent)
    : AbstractTool(parent), m_currentState(SelectingP1),
      m_patternName("ANSI31"), m_patternScale(1.0), m_patternAngle(0.0),
      m_hatchMode(ModeSelect), m_hatchLayer(""),
     m_isDialogShown(false) {} // 플래그 초기화

void HatchTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);

    // [버그 픽스] 포커스 이벤트 등으로 인해 activate가 중복 호출될 경우 튕겨냅니다.
    if (m_isDialogShown) {
        return;
    }
    m_isDialogShown = true;

    IAppContext *ctx = getContext();
    if (!ctx || !ctx->hatchPatternManager()) {
        emit toolFinished();
        return;
    }

    // 다이얼로그 생성 시 컨텍스트가 가진 매니저를 주입
    HatchDialog dialog(ctx->hatchPatternManager());

    // 설정 로드 (3-1: 도면 스코프 우선 → 전역 QSettings → 코드 기본값)
    //   네임스페이스 "Hatch" 는 기존 QSettings 그룹명 그대로.
    ToolDefaults hd("Hatch", m_scene);
    m_patternName  = hd.getString("Pattern", "ANSI31");
    m_patternScale = hd.getDouble("Scale", 1.0);
    m_patternAngle = hd.getDouble("Angle", 0.0);
    int modeVal    = hd.getInt("Mode", (int)ModeSelect);
    m_hatchMode = static_cast<HatchMode>(modeVal);
    m_hatchLayer = hd.getString("Layer", "");

    // 다이얼로그 초기값 세팅
    dialog.setScale(m_patternScale);
    dialog.setAngle(m_patternAngle);
    if (!m_patternName.isEmpty()) {
        dialog.setPattern(m_patternName);
    }
    if (m_hatchMode == ModeBox) dialog.setBoxMode(true);
    else dialog.setBoxMode(false);

    if (ctx && ctx->layerManager()) {
        QStringList layerNames;
        QList<Layer> layers = ctx->layerManager()->getAllLayers();
        for (int i = 0; i < layers.size(); ++i) {
            layerNames << layers[i].name();
        }
        
        // 사용자가 마지막으로 선택한 해치 레이어가 있다면 그것을 우선, 없으면 현재 레이어
        QString defaultLayer = m_hatchLayer;
        if (defaultLayer.isEmpty()) {
            if (Layer* curr = ctx->layerManager()->getCurrentLayer()) {
                defaultLayer = curr->name();
            }
        }
        dialog.initLayers(layerNames, defaultLayer);
    }

    if (dialog.exec() == QDialog::Accepted) {
        m_patternName = dialog.selectedPattern();
        m_patternScale = dialog.selectedScale();
        m_patternAngle = dialog.selectedAngle();
        m_hatchMode = dialog.isBoxMode() ? ModeBox : ModeSelect;
        m_hatchLayer = dialog.selectedLayer();

        // 설정 저장 (도면 스코프 + 전역 씨앗 동시)
        hd.put("Pattern", m_patternName);
        hd.put("Scale",   m_patternScale);
        hd.put("Angle",   m_patternAngle);
        hd.put("Mode",    (int)m_hatchMode);
        hd.put("Layer",   m_hatchLayer);
    } else {
        emit toolFinished();
        return;
    }

    m_currentState = SelectingP1;
    m_selectedItems.clear();

    if (ctx && ctx->commandDock()) {
        QString modeStr = (m_hatchMode == ModeBox) ? "BOX" : "SELECT";
        ctx->commandDock()->logMessage("HATCH (" + modeStr + ") - Pattern: " + m_patternName + ", Layer: " + m_hatchLayer, CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(tr(" 사각상자 모양의 첫번째 점을 지정 하세요 : "), {});
    }
}

void HatchTool::deactivate() {
    if (m_scene) {
        m_scene->clearOverlay();
        m_scene->setGhostPath(QPainterPath(), false);
    }
    AbstractTool::deactivate();
}

void HatchTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton || !m_scene) return;

    IAppContext *ctx = getContext();
    QPointF snappedPos = pos;
    if (ctx && ctx->snapManager()) {
        QPointF base = (m_currentState == SelectingP2) ? m_p1 : QPointF();
        snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    }

    if (m_currentState == SelectingP1) {
        m_p1 = snappedPos;
        m_currentState = SelectingP2;
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 사각상자 모양의 두번째 점을 지정 하세요  : "), {});
        }
    } else if (m_currentState == SelectingP2) {
        m_p2 = snappedPos;
        QRectF rect = QRectF(m_p1, m_p2).normalized();

        if (rect.width() < 1e-5 || rect.height() < 1e-5) {
            if (ctx && ctx->commandDock()) {
                ctx->commandDock()->logMessage(tr(" 상자가 너무 작아요 다시 지정 하세요."), CommandDock::Warning);
            }
            m_currentState = SelectingP1;
            return;
        }

        QList<QGraphicsItem*> items = m_scene->items(rect, Qt::IntersectsItemShape);
        m_selectedItems.clear();
        for (int i = 0; i < items.size(); ++i) {
            if (CadItem* ci = dynamic_cast<CadItem*>(items[i])) {
                m_selectedItems.append(ci);
            }
        }

        m_scene->setGhostPath(QPainterPath(), false);

        if (m_hatchMode == ModeBox) {
            executeBoxHatch(rect);
        } else {
            if (!m_selectedItems.isEmpty()) {
                m_currentState = PickingHatchPoint;
                if (ctx && ctx->commandDock()) {
                    ctx->commandDock()->logMessage(QString::number(m_selectedItems.size()) + " objects selected.", CommandDock::Info);
                    ctx->commandDock()->showInteractivePrompt(tr(" 해칭이될 내부공간을 지정하세요 : "), {});
                }
            } else {
                m_currentState = SelectingP1;
                if (ctx && ctx->commandDock()) {
                    ctx->commandDock()->logMessage(tr(" 객체 선택을 다시 하세요."), CommandDock::Warning);
                }
            }
        }
    } else if (m_currentState == PickingHatchPoint) {
        executeHatch(pos);
    }
}

void HatchTool::mouseMove(QPointF pos) {
    if (!m_scene) return;

    IAppContext *ctx = getContext();
    QPointF snappedPos = pos;
    SnapType snapType = SnapType::None;

    if (ctx && ctx->snapManager()) {
        QPointF base = (m_currentState == SelectingP2) ? m_p1 : QPointF();
        SnapManager::SnapResult result = ctx->snapManager()->snapPoint(pos, m_scene, base);
        snappedPos = result.snappedPos;
        snapType = result.type;
    }

    if (m_currentState == SelectingP2) {
        QRectF rect = QRectF(m_p1, snappedPos).normalized();
        QPainterPath path;
        path.addRect(rect);
        updateGhostPreview(path, "");
    }

    if (m_scene) {
        m_scene->setSnapMarker(snappedPos, snapType);
    }
}

void HatchTool::executeHatch(const QPointF &pickedPoint) {
    QPainterPath boundaryPath = AdvancedBoundaryFinder::findBoundaryPath(m_selectedItems, pickedPoint);

    if (!boundaryPath.isEmpty()) {
        applyHatch(boundaryPath);
    } else {
        IAppContext *ctx = getContext();
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->logMessage(tr("해칭 실패 : 닫친 공간이 찾지 못했어요."), CommandDock::Warning);
        }
        emit toolFinished();
    }
}

void HatchTool::executeBoxHatch(const QRectF &rect) {
    QPainterPath boundaryPath = AdvancedBoundaryFinder::findBoundaryPathInRect(m_selectedItems, rect);
    applyHatch(boundaryPath);
}

// executeHatch/executeBoxHatch의 공통 로직을 하나로 통합한 헬퍼 함수
// - HatchItem 생성
// - 레이어 이름 및 펜(색상/두께) 설정
// - UndoStack에 push (없으면 직접 addItem)
void HatchTool::applyHatch(const QPainterPath &boundaryPath) {
    if (boundaryPath.isEmpty()) {
        emit toolFinished();
        return;
    }

    IAppContext *ctx = getContext();
    HatchItem *hatch = new HatchItem(boundaryPath, m_patternName, m_patternScale, m_patternAngle);
    hatch->setLayerName(m_hatchLayer);

    if (ctx && ctx->layerManager()) {
        if (Layer *L = ctx->layerManager()->getLayer(m_hatchLayer)) {
            QPen p;
            p.setColor(L->color());
            p.setWidthF(L->weight() > 0 ? L->weight() : 0.0);
            p.setCosmetic(true);
            hatch->setPen(p);
        }
    }

    if (ctx && ctx->undoStack()) {
        ctx->undoStack()->push(new AddEntityCommand(m_scene, hatch));
    } else {
        m_scene->addItem(hatch);
    }

    hatch->update();
    m_scene->update();
    emit toolFinished();
}