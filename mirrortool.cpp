#include "mirrortool.h"
#include "blockitem.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "lineitem.h"
#include "multilinetagutil.h"
#include "mygraphicsscene.h"
#include "mymath.h"
#include "snapmanager.h"

#include <QKeyEvent>

MirrorTool::MirrorTool(IAppContext *ctx, QObject *parent)
    : AbstractTool(parent), m_currentState(State_SelectingObjects) {
    setContext(ctx);
}

MirrorTool::~MirrorTool() {}

void MirrorTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene)
        return;
    AbstractTool::activate(scene);

    m_selectedItems.clear();
    if (m_scene) {
        QList<CadItem *> topItems = getTopLevelCadItems(m_scene->selectedItems());
        m_selectedItems.clear();
        // [수정] 인덱스 기반 루프
        for (int i = 0; i < topItems.size(); ++i) {
            m_selectedItems.push_back(topItems[i]);
        }
    }

    if (!m_selectedItems.empty()) {
        m_currentState = State_WaitingForFirstPoint;
    } else {
        m_currentState = State_SelectingObjects;
    }

    if (auto ctx = getContext()) {
        if (auto dock = ctx->commandDock()) {
            dock->setToolActive(true);
            dock->logMessage("MIRROR", CommandDock::Command);
        }
    }
    updatePrompt();
}

void MirrorTool::deactivate() {
    clearOverlay();
    m_selectedItems.clear();
    AbstractTool::deactivate();
}

void MirrorTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (m_currentState == State_SelectingObjects) {
        if (button == Qt::RightButton) {
            if (!m_selectedItems.empty()) {
                m_scene->savePreviousSelection();
                m_currentState = State_WaitingForFirstPoint;
                updatePrompt();
            }
            return;
        }
        if (button == Qt::LeftButton) {
            if (CadItem *item = dynamic_cast<CadItem *>(itemAtPos(pos))) {
                auto it =
                    std::find(m_selectedItems.begin(), m_selectedItems.end(), item);
                if (it == m_selectedItems.end()) {
                    m_selectedItems.push_back(item);
                    item->setSelected(true);
                    hideGripsOfSelectedItems();
                } else {
                    m_selectedItems.erase(it);
                    item->setSelected(false);
                }
            } else {
                m_firstCorner = pos;
                m_currentState = State_SelectingObjects_SecondCorner;
                clearOverlay();
            }
        }
    } else if (m_currentState == State_SelectingObjects_SecondCorner) {
        if (button == Qt::LeftButton) {
            processBoxSelection(pos);
            m_currentState = State_SelectingObjects;
        }
    } else if (m_currentState == State_WaitingForFirstPoint ||
               m_currentState == State_WaitingForSecondPoint) {
        if (button == Qt::LeftButton) {

            QPointF snappedPos = pos;
            bool isObjectSnapped = false; //  OSnap 여부 판단 변수

            if (auto ctx = getContext()) {
                if (auto sm = ctx->snapManager()) {
                    QPointF base = (m_currentState == State_WaitingForSecondPoint)
                    ? m_mirrorP1.toPointF()
                    : QPointF();
                    SnapManager::SnapResult res = sm->snapPoint(pos, m_scene, base);
                    snappedPos = res.snappedPos;

                    //  유효한 스냅이고, Grid나 None이 아닌 '객체 스냅'인 경우 true
                    if (res.isValid && res.type != SnapType::None && res.type != SnapType::Grid) {
                        isObjectSnapped = true;
                    }
                }
            }

            if (m_currentState == State_WaitingForFirstPoint) {
                // [수정] 객체 스냅이 안 잡혔을 때만 선형 객체를 축으로 사용
                if (!isObjectSnapped) {
                    if (CadItem *item = dynamic_cast<CadItem *>(itemAtPos(pos))) {
                        if (LineItem *line = dynamic_cast<LineItem *>(item)) {
                            m_mirrorP1 = line->getLine().start;
                            m_mirrorP2 = line->getLine().end;
                            m_currentState = State_AskingEraseSource;
                            updatePrompt();
                            clearOverlay();
                            return;
                        }
                    }
                }

                // 스냅이 잡혔거나, 빈 공간인 경우 첫 번째 점 지정
                m_mirrorP1 = MyVector(snappedPos);
                m_currentState = State_WaitingForSecondPoint;
                updatePrompt();
            } else {
                // 두 번째 점 지정
                m_mirrorP2 = MyVector(snappedPos);
                if (m_mirrorP1.distanceTo(m_mirrorP2) < 1e-6)
                    return;
                m_currentState = State_AskingEraseSource;
                updatePrompt();
                clearOverlay();
            }
        }
    }
}

void MirrorTool::mouseMove(QPointF pos) {
    if (m_currentState == State_SelectingObjects_SecondCorner) {
        handleSelectionMove(pos);
        return;
    }

    QPointF finalPos = pos;
    SnapType snapType = SnapType::None;
    if (auto ctx = getContext()) {
        if (auto sm = ctx->snapManager()) {
            QPointF base = (m_currentState == State_WaitingForSecondPoint)
            ? m_mirrorP1.toPointF()
            : QPointF();
            SnapManager::SnapResult res = sm->snapPoint(pos, m_scene, base);
            finalPos = res.snappedPos;
            snapType = res.type;
        }
    }

    if (m_scene) {
        m_scene->setSnapMarker(finalPos, snapType);
    }

    if (m_currentState == State_WaitingForSecondPoint) {
        updateMirrorGhost(finalPos);
    }
}

void MirrorTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Space) {
        if (m_currentState == State_SelectingObjects) {
            if (!m_selectedItems.empty()) {
                m_scene->savePreviousSelection();
                m_currentState = State_WaitingForFirstPoint;
                updatePrompt();
            }
        } else if (m_currentState == State_AskingEraseSource) {
            finishMirror(false);
        }
    } else if (event->key() == Qt::Key_Escape) {
        emit toolFinished();
    } else {
        AbstractTool::keyPress(event);
    }
}

bool MirrorTool::processCommand(const QString &command) {
    QString cmd = command.trimmed().toUpper();

    if (m_currentState == State_SelectingObjects) {
        if (cmd == "P") {
            if (m_scene && m_scene->applyPreviousSelection()) {
                m_selectedItems.clear();
                QList<CadItem *> prev = getTopLevelCadItems(m_scene->selectedItems());
                for (int i = 0; i < prev.size(); ++i)
                    m_selectedItems.push_back(prev[i]);
                hideGripsOfSelectedItems();
            }
            return true;
        }
    }

    if (m_currentState == State_AskingEraseSource) {
        if (cmd == "Y" || cmd == "YES") {
            finishMirror(true);
            return true;
        } else if (cmd == "N" || cmd == "NO" || cmd.isEmpty()) {
            finishMirror(false);
            return true;
        }
    }
    return false;
}

void MirrorTool::updatePrompt() {
    auto ctx = getContext();
    if (!ctx || !ctx->commandDock())
        return;
    auto dock = ctx->commandDock();

    switch (m_currentState) {
    case State_SelectingObjects:
    case State_SelectingObjects_SecondCorner:
        dock->showInteractivePrompt(tr("대칭할 객체 선택 하세요 : "), {});
        break;
    case State_WaitingForFirstPoint:
        dock->showInteractivePrompt(tr("대칭축의 첫 번째 점 지정 또는 대칭 객체선을 지정 하세요 : "), {});
        break;
    case State_WaitingForSecondPoint:
        dock->showInteractivePrompt(tr("대칭축의 두 번째 점 지정 하세요 : "), {});
        break;
    case State_AskingEraseSource:
        dock->showInteractivePrompt(
            tr("원본 객체를 삭제하시겠습니까? [예(Y)/아니오(N)] <N>:"), {"Y", "N"});
        break;
    }
}

void MirrorTool::clearOverlay() {
    if (m_scene)
        m_scene->clearOverlay();
}

void MirrorTool::handleSelectionMove(QPointF pos) {
    if (m_currentState != State_SelectingObjects_SecondCorner || !m_scene)
        return;
    bool isCrossing = (pos.x() < m_firstCorner.x());
    m_scene->setSelectionRect(m_firstCorner, pos,
                              isCrossing ? MyGraphicsScene::Mode_Crossing
                                         : MyGraphicsScene::Mode_Window);
}

void MirrorTool::processBoxSelection(QPointF p2) {
    clearOverlay();
    if (!m_scene) return;

    bool isWindow = (p2.x() > m_firstCorner.x());
    QList<CadItem *> topItems = isWindow
        ? collectItemsInWindowRect(m_firstCorner, p2)
        : collectItemsInCrossingRect(m_firstCorner, p2);

    for (int i = 0; i < topItems.size(); ++i) {
        CadItem *ci = topItems[i];
        if (std::find(m_selectedItems.begin(), m_selectedItems.end(), ci) ==
            m_selectedItems.end()) {
            m_selectedItems.push_back(ci);
            ci->setSelected(true);
        }
    }
    hideGripsOfSelectedItems();
}

void MirrorTool::updateMirrorGhost(const QPointF &currentPos) {
    if (!m_scene || m_selectedItems.empty())
        return;

    MyVector p1 = m_mirrorP1;
    MyVector p2(currentPos);

    QString commonLayer = "";
    if (!m_selectedItems.empty()) {
        commonLayer = m_selectedItems[0]->layerName();
        for (size_t i = 1; i < m_selectedItems.size(); ++i) {
            if (m_selectedItems[i]->layerName() != commonLayer) {
                commonLayer = "";
                break;
            }
        }
    }

    updateGhostLinePreview(QLineF(p1.toPointF(), currentPos), "");

    QPainterPath combinedPath;
    // [수정] 인덱스 기반 루프
    for (size_t i = 0; i < m_selectedItems.size(); ++i) {
        if (CadItem *ci = dynamic_cast<CadItem *>(m_selectedItems[i])) {
            combinedPath.addPath(ci->getMirroredPath(p1, p2));
        }
    }
    updateGhostPreview(combinedPath, commonLayer);
}

void MirrorTool::finishMirror(bool eraseSource) {
    if (m_selectedItems.empty() || !m_scene) {
        emit toolFinished();
        return;
    }

    auto ctx = getContext();
    if (!ctx || !ctx->undoStack()) {
        emit toolFinished();
        return;
    }

    ctx->undoStack()->beginMacro(tr("Mirror"));

    // 복사본의 다중선 그룹 ID 재매핑 — 원본과 같은 ID 공유 방지
    QMap<QString, QString> gidRemap;

    // [수정] 인덱스 기반 루프
    for (size_t i = 0; i < m_selectedItems.size(); ++i) {
        CadItem *item = m_selectedItems[i];
        if (eraseSource) {
            ctx->undoStack()->push(
                new MirrorEntityCommand(item, m_mirrorP1, m_mirrorP2));
        } else {
            CadItem *clone = item->clone();
            clone->mirror(m_mirrorP1, m_mirrorP2);
            MultilineTagUtil::remapGroupId(clone, gidRemap);

            clone->setLayerName(item->layerName());
            if (!dynamic_cast<BlockItem *>(clone)) {
                clone->setPen(item->pen());
            }
            ctx->undoStack()->push(new AddEntityCommand(m_scene, clone));
        }
    }

    ctx->undoStack()->endMacro();

    m_scene->clearSelection();
    emit toolFinished();
}