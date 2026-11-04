#include "arraytool.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "mygraphicsscene.h"
#include "multilinetagutil.h"
#include "mymath.h"
#include "snapmanager.h"
#include <QDebug>
#include <QKeyEvent>
#include <QtMath>
#include <QGraphicsItem>

// 정적 변수 초기화
ArrayTool::ArrayType ArrayTool::s_lastArrayType = ArrayTool::Polar;
int ArrayTool::s_lastNumRows = 1;
int ArrayTool::s_lastNumCols = 1;
double ArrayTool::s_lastRowSpacing = 10.0;
double ArrayTool::s_lastColSpacing = 10.0;
int ArrayTool::s_lastNumItems = 4;
double ArrayTool::s_lastFillAngle = 360.0;

ArrayTool::ArrayTool(QObject *parent) : AbstractTool(parent) {}

ArrayTool::~ArrayTool() { clearOverlay(); }

void ArrayTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene)
        return;
    AbstractTool::activate(scene);

    m_selectedItems.clear();
    m_currentState = State_SelectingObjects;

    m_arrayType = s_lastArrayType;
    m_numRows = s_lastNumRows;
    m_numCols = s_lastNumCols;
    m_rowSpacing = s_lastRowSpacing;
    m_colSpacing = s_lastColSpacing;
    m_numItems = s_lastNumItems;
    m_fillAngle = s_lastFillAngle;

    if (m_scene) {
        QList<QGraphicsItem *> selectedItems = m_scene->selectedItems();
        for (int i = 0; i < selectedItems.size(); ++i) {
            if (CadItem *ci = dynamic_cast<CadItem *>(selectedItems[i])) {
                m_selectedItems.push_back(ci);
            }
        }
        if (!m_selectedItems.empty()) {
            hideGripsOfSelectedItems();
            m_currentState = State_SelectingType;
            m_rectBasePoint = m_selectedItems[0]->scenePos();
        }
    }

    if (auto ctx = getContext()) {
        if (auto dock = ctx->commandDock()) {
            dock->setToolActive(true);
            dock->logMessage("ARRAY", CommandDock::Command);
        }
    }
    updatePrompt();
}

void ArrayTool::deactivate() {
    clearOverlay();
    m_selectedItems.clear();
    AbstractTool::deactivate();
}

bool ArrayTool::processCommand(const QString &command) {
    QString cmd = command.toLower().trimmed();
    if (cmd.isEmpty()) {
        if (m_currentState == State_SelectingObjects) {
            if (!m_selectedItems.empty()) {
                m_currentState = State_SelectingType;
                updatePrompt();
                return true;
            }
            return false;
        }

        switch (m_currentState) {
        case State_SelectingType:
            if (m_arrayType == Rectangular) {
                m_currentState = State_Rect_WaitingNumRows;
            } else {
                m_arrayType = Polar;
                m_currentState = State_Polar_WaitingCenter;
            }
            updatePrompt();
            return true;

        case State_Rect_WaitingNumRows:
            m_currentState = State_Rect_WaitingNumCols;
            updatePrompt();
            updateArrayGhost(m_currentMousePos);
            return true;
        case State_Rect_WaitingNumCols:
            m_currentState = State_Rect_WaitingRowSpacing_P1;
            updatePrompt();
            updateArrayGhost(m_currentMousePos);
            return true;
        case State_Rect_WaitingRowSpacing_P1:
            m_currentState = State_Rect_WaitingColSpacing_P1;
            updatePrompt();
            updateArrayGhost(m_currentMousePos);
            return true;
        case State_Rect_WaitingColSpacing_P1:
            finishArray();
            return true;

        case State_Polar_WaitingNumItems:
            m_currentState = State_Polar_WaitingAngle;
            updatePrompt();
            updateArrayGhost(m_currentMousePos);
            return true;
        case State_Polar_WaitingAngle:
            finishArray();
            return true;

        default:
            return false;
        }
    }

    if (m_currentState == State_SelectingType) {
        if (cmd == "r" || cmd == "rect" || cmd == "rectangular") {
            m_arrayType = s_lastArrayType = Rectangular;
            m_currentState = State_Rect_WaitingNumRows;
            updatePrompt();
            return true;
        } else if (cmd == "p" || cmd == "polar") {
            m_arrayType = s_lastArrayType = Polar;
            m_currentState = State_Polar_WaitingCenter;
            updatePrompt();
            return true;
        }
    }

    bool ok;
    double doubleVal = cmd.toDouble(&ok);

    switch (m_currentState) {
    case State_Rect_WaitingNumRows:
        if (ok) {
            m_numRows = s_lastNumRows = std::max(1, (int)doubleVal);
            m_currentState = State_Rect_WaitingNumCols;
            updatePrompt();
            updateArrayGhost(m_currentMousePos);
            return true;
        }
        break;
    case State_Rect_WaitingNumCols:
        if (ok) {
            m_numCols = s_lastNumCols = std::max(1, (int)doubleVal);
            m_currentState = State_Rect_WaitingRowSpacing_P1;
            updatePrompt();
            updateArrayGhost(m_currentMousePos);
            return true;
        }
        break;
    case State_Rect_WaitingRowSpacing_P1:
        if (ok) {
            m_rowSpacing = s_lastRowSpacing = doubleVal;
            m_currentState = State_Rect_WaitingColSpacing_P1;
            updatePrompt();
            updateArrayGhost(m_currentMousePos);
            return true;
        }
        break;
    case State_Rect_WaitingColSpacing_P1:
        if (ok) {
            m_colSpacing = s_lastColSpacing = doubleVal;
            finishArray();
            return true;
        }
        break;
    case State_Polar_WaitingNumItems:
        if (ok) {
            m_numItems = s_lastNumItems = std::max(2, (int)doubleVal);
            m_currentState = State_Polar_WaitingAngle;
            updatePrompt();
            updateArrayGhost(m_currentMousePos);
            return true;
        }
        break;
    case State_Polar_WaitingAngle:
        if (ok) {
            m_fillAngle = s_lastFillAngle = doubleVal;
            finishArray();
            return true;
        }
        break;
    default:
        break;
    }

    IAppContext *ctx = getContext();
    QPointF resultPos;
    if (ctx && ctx->parser() &&
        ctx->parser()->parseCoordinate(cmd, QPointF(), m_currentMousePos, false, resultPos)) {
        m_currentMousePos = resultPos;
        mousePress(resultPos, Qt::LeftButton);
        updateArrayGhost(m_currentMousePos);
        return true;
    }

    return false;
}

void ArrayTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (button == Qt::RightButton && m_currentState == State_SelectingObjects) {
        if (!m_selectedItems.empty()) {
            m_currentState = State_SelectingType;
            updatePrompt();
        }
        return;
    }

    if (button != Qt::LeftButton)
        return;

    // 1. 수동 직교 모드 제거 및 SnapManager 전적으로 활용
    QPointF snappedPos = pos;
    IAppContext *ctx = getContext();

    if (ctx && ctx->snapManager() && m_scene) {
        QPointF basePoint;
        if (m_currentState == State_SelectingObjects_SecondCorner) {
            basePoint = m_firstCorner;
        } else if (m_currentState == State_Rect_WaitingRowSpacing_P2 ||
                   m_currentState == State_Rect_WaitingColSpacing_P2) {
            basePoint = m_spacingPoint1;
        } else if (m_currentState == State_Polar_WaitingAngle) {
            basePoint = m_centerPoint;
        }
        // SnapManager가 알아서 OSNAP과 직교 모드(Ortho)를 반영해줍니다.
        snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, basePoint).snappedPos;
    }

    switch (m_currentState) {
    case State_SelectingObjects:
        if (auto *item = itemAtPos(pos)) {
            if (CadItem *ci = dynamic_cast<CadItem *>(item)) {
                auto it = std::find(m_selectedItems.begin(), m_selectedItems.end(), ci);
                if (it == m_selectedItems.end()) {
                    m_selectedItems.push_back(ci);
                    ci->setSelected(true);
                    hideGripsOfSelectedItems();
                } else {
                    m_selectedItems.erase(it);
                    ci->setSelected(false);
                }
            }
        } else {
            m_firstCorner = snappedPos;
            m_currentState = State_SelectingObjects_SecondCorner;
        }
        break;
    case State_SelectingObjects_SecondCorner:
        processBoxSelection(snappedPos);
        m_currentState = State_SelectingObjects;
        break;
    case State_Polar_WaitingCenter:
        m_centerPoint = snappedPos;
        m_currentState = State_Polar_WaitingNumItems;
        break;
    case State_Rect_WaitingRowSpacing_P1:
        m_spacingPoint1 = snappedPos;
        m_currentState = State_Rect_WaitingRowSpacing_P2;
        break;
    case State_Rect_WaitingRowSpacing_P2:
        m_rowSpacing = s_lastRowSpacing = snappedPos.y() - m_spacingPoint1.y();
        m_currentState = State_Rect_WaitingColSpacing_P1;
        break;
    case State_Rect_WaitingColSpacing_P1:
        m_spacingPoint1 = snappedPos;
        m_currentState = State_Rect_WaitingColSpacing_P2;
        break;
    case State_Rect_WaitingColSpacing_P2:
        m_colSpacing = s_lastColSpacing = snappedPos.x() - m_spacingPoint1.x();
        finishArray();
        return;
    case State_Polar_WaitingAngle:
        m_fillAngle = qRadiansToDegrees(
            std::atan2(snappedPos.y() - m_centerPoint.y(), snappedPos.x() - m_centerPoint.x()));
        finishArray();
        return;
    default:
        break;
    }
    updatePrompt();
}

void ArrayTool::mouseMove(QPointF pos) {
    QPointF finalPos = pos;
    SnapType snapType = SnapType::None;

    IAppContext *ctx = getContext();
    if (ctx && ctx->snapManager() && m_scene) {
        QPointF basePoint;
        if (m_currentState == State_SelectingObjects_SecondCorner) {
            basePoint = m_firstCorner;
        } else if (m_currentState == State_Rect_WaitingRowSpacing_P2 ||
                   m_currentState == State_Rect_WaitingColSpacing_P2) {
            basePoint = m_spacingPoint1;
        } else if (m_currentState == State_Polar_WaitingAngle) {
            basePoint = m_centerPoint;
        }

        // SnapManager 호출하여 직교 및 스냅 정보 획득
        SnapManager::SnapResult result = ctx->snapManager()->snapPoint(pos, m_scene, basePoint);
        finalPos = result.snappedPos;
        snapType = result.type;
    }

    m_currentMousePos = finalPos;

    // 2. 고스트(미리보기) 렌더링 먼저 실행
    // updateArrayGhost 내부에 clearOverlay()가 호출되어 모든 오버레이(스냅 마커 포함)를 지우게 됩니다.
    if (m_currentState == State_SelectingObjects_SecondCorner) {
        handleSelectionMove(finalPos);
    } else if (m_currentState == State_Rect_WaitingRowSpacing_P2 ||
               m_currentState == State_Rect_WaitingColSpacing_P2) {
        clearOverlay();
        m_scene->setGhostLine(QLineF(m_spacingPoint1, finalPos), true);
        updateArrayGhost(finalPos);
    } else if (m_currentState >= State_Rect_WaitingNumRows ||
               m_currentState >= State_Polar_WaitingCenter) {
        updateArrayGhost(finalPos);
    }

    // 3. 스냅 마커를 마지막에 표시 (clearOverlay에 의해 마커가 지워지는 현상 방지)
    if (m_scene) {
        m_scene->setSnapMarker(finalPos, snapType);
    }
}

void ArrayTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        emit toolFinished();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
               event->key() == Qt::Key_Space) {
        if (m_currentState == State_SelectingObjects) {
            if (!m_selectedItems.empty()) {
                m_currentState = State_SelectingType;
                updatePrompt();
            } else {
                emit toolFinished();
            }
        } else {
            processCommand("");
        }
    } else {
        AbstractTool::keyPress(event);
    }
}

void ArrayTool::updatePrompt() {
    auto ctx = getContext();
    if (!ctx || !ctx->commandDock())
        return;
    auto dock = ctx->commandDock();

    switch (m_currentState) {
    case State_SelectingObjects:
        dock->showInteractivePrompt(tr("배열할 객체 선택 (선택완료:엔터):"), {});
        break;
    case State_SelectingObjects_SecondCorner:
        dock->showInteractivePrompt(tr("다음 구석 지정:"), {});
        break;
    case State_SelectingType: {
        QString def = (m_arrayType == Rectangular) ? "Rectangular" : "Polar";
        dock->showInteractivePrompt(
            tr("배열 유형 입력 [직사각형(R)/원형(P)] <%1>:").arg(def),
            {"Rectangular", "Polar"});
        break;
    }
    case State_Rect_WaitingNumRows:
        dock->showInteractivePrompt(tr("행(Rows) 개수 입력 <%1>:").arg(m_numRows), {});
        break;
    case State_Rect_WaitingNumCols:
        dock->showInteractivePrompt(tr("열(Cols) 개수 입력 <%1>:").arg(m_numCols), {});
        break;
    case State_Rect_WaitingRowSpacing_P1:
        dock->showInteractivePrompt(
            tr("행 간격 지정 또는 첫 번째 점 클릭 <%1>:").arg(m_rowSpacing), {});
        break;
    case State_Rect_WaitingRowSpacing_P2:
        dock->showInteractivePrompt(tr("행 간격을 위한 두 번째 점 클릭:"), {});
        break;
    case State_Rect_WaitingColSpacing_P1:
        dock->showInteractivePrompt(
            tr("열 간격 지정 또는 첫 번째 점 클릭 <%1>:").arg(m_colSpacing), {});
        break;
    case State_Rect_WaitingColSpacing_P2:
        dock->showInteractivePrompt(tr("열 간격을 위한 두 번째 점 클릭:"), {});
        break;
    case State_Polar_WaitingCenter:
        dock->showInteractivePrompt(tr("배열의 중심점 지정:"), {});
        break;
    case State_Polar_WaitingNumItems:
        dock->showInteractivePrompt(tr("항목 개수 입력 <%1>:").arg(m_numItems), {});
        break;
    case State_Polar_WaitingAngle:
        dock->showInteractivePrompt(
            tr("채울 각도 지정 또는 클릭 <%1>:").arg(m_fillAngle), {});
        break;
    }
}

void ArrayTool::clearOverlay() {
    if (m_scene)
        m_scene->clearOverlay();
}

void ArrayTool::handleSelectionMove(QPointF pos) {
    if (!m_scene)
        return;
    bool isCrossing = (pos.x() < m_firstCorner.x());
    m_scene->setSelectionRect(m_firstCorner, pos,
                              isCrossing ? MyGraphicsScene::Mode_Crossing
                                         : MyGraphicsScene::Mode_Window);
}

void ArrayTool::processBoxSelection(QPointF p2) {
    if (!m_scene) return;

    bool isWindow = (p2.x() > m_firstCorner.x());
    QList<CadItem *> topItems = isWindow
                                    ? collectItemsInWindowRect(m_firstCorner, p2)
                                    : collectItemsInCrossingRect(m_firstCorner, p2);

    for (int i = 0; i < topItems.size(); ++i) {
        CadItem *ci = topItems.at(i);
        if (std::find(m_selectedItems.begin(), m_selectedItems.end(), ci) == m_selectedItems.end()) {
            m_selectedItems.push_back(ci);
            ci->setSelected(true);
        }
    }
    hideGripsOfSelectedItems();
    clearOverlay();
}

void ArrayTool::updateArrayGhost(const QPointF &currentPos) {
    clearOverlay();
    if (!m_scene || m_selectedItems.empty())
        return;

    QPainterPath combinedPath;
    QList<QGraphicsItem *> graphicsList;
    for (int i = 0; i < m_selectedItems.size(); ++i) {
        graphicsList.append(m_selectedItems[i]);
    }
    QString commonLayer = getCommonLayerName(graphicsList);

    if (m_arrayType == Rectangular) {
        double rSpacing = m_rowSpacing;
        double cSpacing = m_colSpacing;

        if (m_currentState == State_Rect_WaitingRowSpacing_P2)
            rSpacing = currentPos.y() - m_spacingPoint1.y();
        if (m_currentState == State_Rect_WaitingColSpacing_P2)
            cSpacing = currentPos.x() - m_spacingPoint1.x();

        for (int r = 0; r < m_numRows; ++r) {
            for (int c = 0; c < m_numCols; ++c) {
                if (r == 0 && c == 0)
                    continue;
                MyVector delta(c * cSpacing, r * rSpacing);
                for (int i = 0; i < m_selectedItems.size(); ++i) {
                    CadItem *item = m_selectedItems[i];
                    combinedPath.addPath(item->getMovedPath(delta));
                }
            }
        }
    } else if (m_arrayType == Polar) {
        int count = m_numItems;
        double angle = m_fillAngle;
        if (m_currentState == State_Polar_WaitingAngle) {
            angle = qRadiansToDegrees(std::atan2(currentPos.y() - m_centerPoint.y(),
                                                 currentPos.x() - m_centerPoint.x()));
        }

        double step = angle / (count > 1 ? count : 1);
        if (std::abs(angle) >= 360.0)
            step = 360.0 / count;

        for (int i = 1; i < count; ++i) {
            double curAngle = i * step;
            for (int j = 0; j < m_selectedItems.size(); ++j) {
                CadItem *item = m_selectedItems[j];
                combinedPath.addPath(item->getRotatedPath(m_centerPoint, curAngle));
            }
        }
    }

    updateGhostPreview(combinedPath, commonLayer);
}

void ArrayTool::finishArray() {
    auto ctx = getContext();
    if (!ctx || !ctx->undoStack() || !m_scene || m_selectedItems.empty()) {
        emit toolFinished();
        return;
    }

    auto stack = ctx->undoStack();
    stack->beginMacro(tr("Array %1 items").arg(m_selectedItems.size()));

    if (m_arrayType == Rectangular) {
        for (int r = 0; r < m_numRows; ++r) {
            for (int c = 0; c < m_numCols; ++c) {
                if (r == 0 && c == 0)
                    continue;
                MyVector delta(c * m_colSpacing, r * m_rowSpacing);
                QMap<QString, QString> gidRemap; // 복사본(셀)마다 새 그룹 ID
                for (int i = 0; i < m_selectedItems.size(); ++i) {
                    CadItem *item = m_selectedItems[i];
                    CadItem *newItem = item->clone();
                    newItem->move(delta);
                    MultilineTagUtil::remapGroupId(newItem, gidRemap);
                    stack->push(new AddEntityCommand(m_scene, newItem));
                }
            }
        }
    } else if (m_arrayType == Polar) {
        double step = m_fillAngle / m_numItems;
        if (std::abs(m_fillAngle) >= 360.0)
            step = 360.0 / m_numItems;

        for (int i = 1; i < m_numItems; ++i) {
            double curAngle = i * step;
            QMap<QString, QString> gidRemap; // 복사본마다 새 그룹 ID
            for (int j = 0; j < m_selectedItems.size(); ++j) {
                CadItem *item = m_selectedItems[j];
                CadItem *newItem = item->clone();
                newItem->rotate(m_centerPoint, curAngle);
                MultilineTagUtil::remapGroupId(newItem, gidRemap);
                stack->push(new AddEntityCommand(m_scene, newItem));
            }
        }
    }

    stack->endMacro();
    m_scene->clearSelection();
    clearOverlay();
    emit toolFinished();
}