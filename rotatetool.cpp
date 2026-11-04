#include "rotatetool.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include <QKeyEvent>
#include <QtMath>
#include <algorithm>

RotateTool::RotateTool(IAppContext *ctx, QObject *parent)
    : AbstractTool(parent), m_currentState(State_SelectingObjects) {
    setContext(ctx);
}

RotateTool::~RotateTool() {}

void RotateTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene)
        return;
    AbstractTool::activate(scene);

    m_selectedItems.clear();
    if (m_scene) {
        QList<QGraphicsItem *> selected = m_scene->selectedItems();
        for (int i = 0; i < selected.size(); ++i) {
            if (CadItem *ci = dynamic_cast<CadItem *>(selected[i])) {
                m_selectedItems.push_back(ci);
            }
        }
    }

    m_currentState = m_selectedItems.empty() ? State_SelectingObjects
                                             : State_WaitingForBasePoint;
    updatePrompt();
}

void RotateTool::deactivate() {
    clearOverlay();
    if (m_scene)
        m_scene->clearOverlay();
    for (size_t i = 0; i < m_selectedItems.size(); ++i) {
        m_selectedItems[i]->setSelected(false);
    }
    m_selectedItems.clear();
    AbstractTool::deactivate();
}

QPointF RotateTool::getSnappedPoint(const QPointF &pos, const QPointF &base) {
    auto ctx = getContext();
    if (!ctx || !ctx->snapManager() || !m_scene)
        return pos;

    QPointF finalPos = pos;
    SnapType snapType = SnapType::None;

    SnapManager::SnapResult res =
        ctx->snapManager()->snapPoint(pos, m_scene, base);
    finalPos = res.snappedPos;
    snapType = res.type;

    if (res.isValid && snapType != SnapType::Grid) {
        // 객체 스냅이 직교보다 우선
        m_scene->setSnapMarker(finalPos, snapType);
        return finalPos;
    }

    // 직교 모드(F8 / Shift): 기준점이 원점(0,0)이어도 적용되도록 직접 보정
    if (m_currentState == State_WaitingForAngle &&
        ctx->snapManager()->isOrthoActive()) {
        if (qAbs(finalPos.x() - base.x()) > qAbs(finalPos.y() - base.y()))
            finalPos.setY(base.y());
        else
            finalPos.setX(base.x());
    }

    if (res.isValid)
        m_scene->setSnapMarker(finalPos, snapType);
    else
        m_scene->clearOverlay();
    return finalPos;
}

void RotateTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (button == Qt::RightButton && m_currentState == State_SelectingObjects) {
        if (!m_selectedItems.empty()) {
            m_scene->savePreviousSelection();
            m_currentState = State_WaitingForBasePoint;
            updatePrompt();
        }
        return;
    }

    if (button != Qt::LeftButton)
        return;

    if (m_currentState == State_SelectingObjects) {
        if (CadItem *item = dynamic_cast<CadItem *>(itemAtPos(pos))) {
            auto it = std::find(m_selectedItems.begin(), m_selectedItems.end(), item);
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
            updatePrompt();
        }
    } else if (m_currentState == State_SelectingObjects_SecondCorner) {
        processBoxSelection(pos);
        m_currentState = State_SelectingObjects;
        updatePrompt();
    } else if (m_currentState == State_WaitingForBasePoint) {
        m_basePoint = MyVector(getSnappedPoint(pos));
        m_currentState = State_WaitingForAngle;
        updatePrompt();
    } else if (m_currentState == State_WaitingForAngle) {
        QPointF snapped = getSnappedPoint(pos, m_basePoint.toPointF());
        double dx = snapped.x() - m_basePoint.x;
        double dy = snapped.y() - m_basePoint.y;
        finishRotate(qRadiansToDegrees(std::atan2(dy, dx)));
    }
}

void RotateTool::mouseMove(QPointF pos) {
    m_lastMousePos = pos;
    if (m_currentState == State_SelectingObjects_SecondCorner) {
        handleSelectionMove(pos);
    } else if (m_currentState == State_WaitingForBasePoint) {
        getSnappedPoint(pos);
    } else if (m_currentState == State_WaitingForAngle) {
        QPointF snapped = getSnappedPoint(pos, m_basePoint.toPointF());
        updateRotateGhost(snapped);
    }
}

void RotateTool::processBoxSelection(QPointF p2) {
    if (!m_scene) return;
    bool isWindow = (p2.x() > m_firstCorner.x());

    QList<CadItem *> topItems = isWindow
        ? collectItemsInWindowRect(m_firstCorner, p2)
        : collectItemsInCrossingRect(m_firstCorner, p2);

    for (int i = 0; i < topItems.size(); ++i) {
        CadItem *ci = topItems[i];
        m_selectedItems.push_back(ci);
        ci->setSelected(true);
    }
    clearOverlay();
}

void RotateTool::updateRotateGhost(const QPointF &currentPos) {
    clearOverlay();
    if (m_selectedItems.empty())
        return;

    double angle = qRadiansToDegrees(
        std::atan2(currentPos.y() - m_basePoint.y, currentPos.x() - m_basePoint.x));

    updateGhostLinePreview(QLineF(m_basePoint.toPointF(), currentPos), "");

    QPainterPath combinedPath;
    for (size_t i = 0; i < m_selectedItems.size(); ++i) {
        combinedPath.addPath(
            m_selectedItems[i]->getRotatedPath(m_basePoint, angle));
    }
    updateGhostPreview(combinedPath, "");
}

void RotateTool::finishRotate(double angle) {
    if (auto ctx = getContext()) {
        if (auto stack = ctx->undoStack()) {
            stack->beginMacro(tr("Rotate"));
            for (size_t i = 0; i < m_selectedItems.size(); ++i) {
                stack->push(
                    new RotateEntityCommand(m_selectedItems[i], m_basePoint, angle));
            }
            stack->endMacro();
        }
    }
    m_scene->clearOverlay();
    emit toolFinished();
}

bool RotateTool::processCommand(const QString &command) {
    if (m_currentState == State_SelectingObjects) {
        QString cmd = command.trimmed().toLower();
        if (cmd == "p") {
            if (m_scene && m_scene->applyPreviousSelection()) {
                m_selectedItems.clear();
                QList<CadItem *> prev = getTopLevelCadItems(m_scene->selectedItems());
                for (int i = 0; i < prev.size(); ++i)
                    m_selectedItems.push_back(prev[i]);
                hideGripsOfSelectedItems();
            }
            return true;
        }
        if (command.isEmpty()) {
            if (!m_selectedItems.empty()) {
                m_scene->savePreviousSelection();
                m_currentState = State_WaitingForBasePoint;
                updatePrompt();
            }
            return true;
        }
    }

    if (m_currentState == State_WaitingForAngle) {
        bool ok;
        double angle = command.toDouble(&ok);
        if (ok) {
            finishRotate(angle);
            return true;
        }
    }
    return false;
}

void RotateTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape)
        emit toolFinished();
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
             event->key() == Qt::Key_Space) {
        processCommand("");
    } else if (event->key() == Qt::Key_F3 || event->key() == Qt::Key_F8 ||
               (event->modifiers() & Qt::AltModifier)) {
        // F3(객체스냅) / F8(직교) / Alt 스냅 토글은 기본 클래스에서 처리
        AbstractTool::keyPress(event);
        // 토글 즉시 고스트 갱신
        if ((event->key() == Qt::Key_F8 || event->key() == Qt::Key_F3) &&
            m_currentState == State_WaitingForAngle) {
            mouseMove(m_lastMousePos);
        }
    }
}

void RotateTool::mouseRelease(QPointF, Qt::MouseButton) {}

void RotateTool::clearOverlay() {
    if (m_scene)
        m_scene->clearOverlay();
}

void RotateTool::handleSelectionMove(QPointF pos) {
    if (m_scene) {
        bool isCrossing = (pos.x() < m_firstCorner.x());
        m_scene->setSelectionRect(m_firstCorner, pos,
                                  isCrossing ? MyGraphicsScene::Mode_Crossing
                                             : MyGraphicsScene::Mode_Window);
    }
}

//  히스토리와 프롬프트 라벨 모두에 안내 메시지를 전달하도록 변경
void RotateTool::updatePrompt() {
    auto ctx = getContext();
    if (!ctx) return;
    auto dock = ctx->commandDock();
    if (!dock) return;

    QString promptMsg;

    switch (m_currentState) {
    case State_SelectingObjects:
        promptMsg = tr("회전할 객체 선택 (완료하려면 우클릭 또는 Enter) : ");
        break;
    case State_SelectingObjects_SecondCorner:
        promptMsg = tr("다음 구석 지정 : ");
        break;
    case State_WaitingForBasePoint:
        promptMsg = tr("기준점 지정 : ");
        break;
    case State_WaitingForAngle:
        promptMsg = tr("회전 각도 지정 또는 클릭 : ");
        break;
    }

    if (!promptMsg.isEmpty()) {
        // showInteractivePrompt를 사용하면 히스토리 로그와 프롬프트 업데이트가 동시에 수행됩니다.
        dock->showInteractivePrompt(promptMsg, QStringList());
    }
}