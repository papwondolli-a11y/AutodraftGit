#include "chamfertool.h"
#include "appcontext.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "mygraphicsscene.h"
#include "myline.h"
#include "mymath.h"
#include <QApplication> // Shift 키 상태 확인을 위해 추가
#include <QKeyEvent>
#include <cmath>

double ChamferTool::s_lastDist1 = 10.0;
double ChamferTool::s_lastDist2 = 10.0;

ChamferTool::ChamferTool(QObject *parent)
    : AbstractTool(parent), m_currentState(State_WaitingForFirst),
    m_firstItem(nullptr), m_dist1(s_lastDist1), m_dist2(s_lastDist2),
    m_isBoxSelecting(false) {}

ChamferTool::~ChamferTool() { clearOverlay(); }

void ChamferTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene)
        return;
    AbstractTool::activate(scene);
    m_currentState = State_WaitingForFirst;
    m_firstItem = nullptr;
    m_dist1 = s_lastDist1;
    m_dist2 = s_lastDist2;
    m_isBoxSelecting = false;
    hideGripsOfSelectedItems();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("CHAMFER", CommandDock::Command);
    }
    updatePrompt();
}

void ChamferTool::deactivate() {
    if (m_firstItem) {
        m_firstItem->setGripsEnabled(true);
        m_firstItem->setSelected(false);
    }
    clearOverlay();
    clearHoverPreview();
    m_firstItem = nullptr;
    s_lastDist1 = m_dist1;
    s_lastDist2 = m_dist2;
    AbstractTool::deactivate();
}

void ChamferTool::updatePrompt() {
    if (!m_appContext)
        return;
    auto dock = m_appContext->commandDock();
    if (!dock)
        return;

    QString distStr = QString("%1, %2").arg(m_dist1).arg(m_dist2);
    if (m_currentState == State_WaitingForFirst) {
        dock->showInteractivePrompt(
            tr("첫번째 선을 선택 하세요 (Distances=%1) [Distance] :").arg(distStr), {"D"});
    } else if (m_currentState == State_WaitingForSecond) {
        // Shift 키 안내 문구 추가
        dock->showInteractivePrompt(tr("두번째 선을 선택하세요 (Shift: 원본 유지) :"), {});
    } else if (m_currentState == State_WaitingForDist1) {
        dock->showInteractivePrompt(tr("첫번째선 모따기 거리를 입력 하세요 <%1>:").arg(m_dist1), {});
    } else if (m_currentState == State_WaitingForDist2) {
        dock->showInteractivePrompt(tr("두번째선 모따기 거리를 입력 하세요 <%1>:").arg(m_dist2), {});
    }
}

void ChamferTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (button == Qt::RightButton) {
        if (m_currentState == State_WaitingForDist1 || m_currentState == State_WaitingForDist2) {
            acceptDefaultDistance();
            return;
        }
        emit toolFinished();
        return;
    }

    if (button != Qt::LeftButton)
        return;

    if (!m_scene)
        return;

    if (m_isBoxSelecting) {
        processBoxSelection(pos);
        return;
    }

    QGraphicsItem *item = itemAtPos(pos);
    CadItem *cadItem = dynamic_cast<CadItem *>(item);

    if (cadItem) {
        const MyEntity *ent = cadItem->getEntityData();
        if (ent && ent->type() == EntityType::Line) {
            if (m_currentState == State_WaitingForFirst) {
                m_firstItem = cadItem;
                m_firstItem->setGripsEnabled(false);
                m_firstItem->setSelected(true);
                m_firstPickPos = pos;
                m_currentState = State_WaitingForSecond;
                updatePrompt();
            } else {
                if (cadItem != m_firstItem) {
                    if (m_firstItem) {
                        m_firstItem->setGripsEnabled(true);
                        m_firstItem->setSelected(false);
                    }

                    // Shift 키가 눌려있으면 trimLines를 false로 전달
                    bool trimLines = !(QApplication::keyboardModifiers() & Qt::ShiftModifier);
                    tryChamfer(m_firstItem, cadItem, m_firstPickPos, pos, trimLines);

                    m_currentState = State_WaitingForFirst;
                    m_firstItem = nullptr;
                    updatePrompt();
                }
            }
        }
    } else {
        m_isBoxSelecting = true;
        m_firstCorner = pos;
        clearOverlay();
    }
}

void ChamferTool::mouseMove(QPointF pos) {
    if (m_isBoxSelecting) {
        handleSelectionMove(pos);
    } else if (m_currentState == State_WaitingForSecond) {
        updateHoverPreview(pos);
    }
}

void ChamferTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
    Q_UNUSED(pos);
    Q_UNUSED(button);
}

void ChamferTool::handleSelectionMove(QPointF pos) {
    if (!m_scene)
        return;
    m_scene->setSelectionRect(m_firstCorner, pos, MyGraphicsScene::Mode_Crossing);
}

void ChamferTool::processBoxSelection(QPointF p2) {
    m_isBoxSelecting = false;
    clearOverlay();

    QRectF rect = QRectF(m_firstCorner, p2).normalized();
    QList<QGraphicsItem *> items = m_scene->items(rect, Qt::IntersectsItemShape);

    std::vector<CadItem *> candidates;
    // C++11 index-based loop 적용
    for (int i = 0; i < items.size(); ++i) {
        QGraphicsItem *item = items[i];
        if (CadItem *ci = dynamic_cast<CadItem *>(item)) {
            const MyEntity *ent = ci->getEntityData();
            if (ent && ent->type() == EntityType::Line) {
                candidates.push_back(ci);
            }
        }
    }

    if (candidates.size() >= 2) {
        CadItem *item1 = candidates[0];
        CadItem *item2 = nullptr;
        for (size_t i = 1; i < candidates.size(); ++i) {
            if (candidates[i] != item1) {
                item2 = candidates[i];
                break;
            }
        }

        if (item1 && item2) {
            // Shift 키 확인
            bool trimLines = !(QApplication::keyboardModifiers() & Qt::ShiftModifier);
            tryChamfer(item1, item2, item1->sceneBoundingRect().center(),
                       item2->sceneBoundingRect().center(), trimLines);
            m_currentState = State_WaitingForFirst;
            m_firstItem = nullptr;
        }
    } else if (candidates.size() == 1) {
        m_firstItem = candidates[0];
        m_firstItem->setGripsEnabled(false);
        m_firstItem->setSelected(true);
        m_firstPickPos = candidates[0]->sceneBoundingRect().center();
        m_currentState = State_WaitingForSecond;
    }
    updatePrompt();
}

void ChamferTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        emit toolFinished();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
               event->key() == Qt::Key_Space) {
        if (m_currentState == State_WaitingForDist1 || m_currentState == State_WaitingForDist2) {
            acceptDefaultDistance();
        } else {
            emit toolFinished();
        }
    } else {
        AbstractTool::keyPress(event);
    }
}

bool ChamferTool::processCommand(const QString &command) {
    QString cmd = command.toUpper().trimmed();
    if (cmd.isEmpty()) {
        if (m_currentState == State_WaitingForDist1 || m_currentState == State_WaitingForDist2) {
            acceptDefaultDistance();
            return true;
        }
        return false;
    }

    if (cmd == "D" || cmd == "DISTANCE") {
        m_currentState = State_WaitingForDist1;
        updatePrompt();
        return true;
    }

    bool ok;
    double d = cmd.toDouble(&ok);
    if (ok) {
        if (m_currentState == State_WaitingForDist1) {
            m_dist1 = std::abs(d);
            s_lastDist1 = m_dist1;
            m_currentState = State_WaitingForDist2;
            updatePrompt();
        } else if (m_currentState == State_WaitingForDist2) {
            m_dist2 = std::abs(d);
            s_lastDist2 = m_dist2;
            m_currentState = State_WaitingForFirst;
            updatePrompt();
        } else {
            m_dist1 = std::abs(d);
            m_dist2 = m_dist1;
            s_lastDist1 = m_dist1;
            s_lastDist2 = m_dist2;
            updatePrompt();
        }
        return true;
    }

    return false;
}

void ChamferTool::clearOverlay() {
    if (m_scene) {
        m_scene->clearOverlay();
    }
}

void ChamferTool::tryChamfer(CadItem *item1, CadItem *item2, const QPointF &p1,
                             const QPointF &p2, bool trimLines) {
    if (!item1 || !item2 || item1 == item2)
        return;
    executeChamfer(item1, item2, p1, p2, trimLines);
}

void ChamferTool::executeChamfer(CadItem *item1, CadItem *item2,
                                 const QPointF &p1, const QPointF &p2, bool trimLines) {
    const MyEntity *e1 = item1->getEntityData();
    const MyEntity *e2 = item2->getEntityData();

    if (e1->type() == EntityType::Line && e2->type() == EntityType::Line) {
        const MyLine *l1 = static_cast<const MyLine *>(e1);
        const MyLine *l2 = static_cast<const MyLine *>(e2);

        std::vector<MyVector> inters = MyMath::intersectLineLineInfinite(*l1, *l2);
        if (inters.empty())
            return;

        MyVector P = inters[0];

        MyVector v1 = (l1->end - l1->start).normalized();
        MyVector v2 = (l2->end - l2->start).normalized();

        MyVector clickDir1 = MyVector(p1) - P;
        MyVector d1 = (MyMath::dotProduct(clickDir1, v1) >= 0) ? v1 : MyVector(-v1.x, -v1.y);

        MyVector clickDir2 = MyVector(p2) - P;
        MyVector d2 = (MyMath::dotProduct(clickDir2, v2) >= 0) ? v2 : MyVector(-v2.x, -v2.y);

        MyVector T1 = P + d1 * m_dist1;
        MyVector T2 = P + d2 * m_dist2;

        QUndoStack *stack = m_appContext->undoStack();
        if (stack)
            stack->beginMacro("Chamfer");

        bool isFirstEnd1 = (MyMath::dotProduct(l1->start - P, d1) < MyMath::dotProduct(l1->end - P, d1));
        QPointF oldPos1 = isFirstEnd1 ? l1->start.toPointF() : l1->end.toPointF();

        bool isFirstEnd2 = (MyMath::dotProduct(l2->start - P, d2) < MyMath::dotProduct(l2->end - P, d2));
        QPointF oldPos2 = isFirstEnd2 ? l2->start.toPointF() : l2->end.toPointF();

        // trimLines가 true일 때만 기존 선을 자르거나 연장합니다.
        if (trimLines) {
            if (stack) {
                stack->push(new ExtendEntityCommand(item1, oldPos1, T1.toPointF(), isFirstEnd1));
                stack->push(new ExtendEntityCommand(item2, oldPos2, T2.toPointF(), isFirstEnd2));
            } else {
                item1->updateEndpoint(T1.toPointF(), isFirstEnd1);
                item2->updateEndpoint(T2.toPointF(), isFirstEnd2);
            }
        }

        // 모따기 선(Chamfer Line)은 trim 여부와 관계없이 생성합니다.
        auto line = m_appContext->entityFactory()->createLine(QLineF(T1.toPointF(), T2.toPointF()), item1->pen());
        if (line) {
            if (CadItem *ci = dynamic_cast<CadItem *>(line)) {
                ci->setLayerName(item1->layerName());
                QGraphicsItem *layerFolder = m_scene->getLayerRoot(item1->layerName());
                ci->setParentItem(layerFolder);
                if (ci->scene() == m_scene) m_scene->removeItem(ci);

                if (stack) stack->push(new AddEntityCommand(m_scene, ci));
                else m_scene->addItem(ci);
            }
        }

        if (stack) stack->endMacro();

        item1->setGripsEnabled(true);
        item1->setSelected(false);
        item2->setGripsEnabled(true);
        item2->setSelected(false);
        clearHoverPreview();
    }
}

void ChamferTool::updateHoverPreview(const QPointF &pos) {
    if (!m_scene || !m_firstItem)
        return;

    QGraphicsItem *item = itemAtPos(pos);
    CadItem *cadItem = dynamic_cast<CadItem *>(item);
    if (!cadItem || cadItem == m_firstItem) {
        clearHoverPreview();
        return;
    }

    const MyEntity *e1 = m_firstItem->getEntityData();
    const MyEntity *e2 = cadItem->getEntityData();

    if (e1->type() == EntityType::Line && e2->type() == EntityType::Line) {
        const MyLine *l1 = static_cast<const MyLine *>(e1);
        const MyLine *l2 = static_cast<const MyLine *>(e2);

        std::vector<MyVector> inters = MyMath::intersectLineLineInfinite(*l1, *l2);
        if (inters.empty()) return;

        MyVector P = inters[0];
        MyVector v1 = (l1->end - l1->start).normalized();
        MyVector v2 = (l2->end - l2->start).normalized();

        MyVector clickDir1 = MyVector(m_firstPickPos) - P;
        MyVector d1 = (MyMath::dotProduct(clickDir1, v1) >= 0) ? v1 : MyVector(-v1.x, -v1.y);

        MyVector clickDir2 = MyVector(pos) - P;
        MyVector d2 = (MyMath::dotProduct(clickDir2, v2) >= 0) ? v2 : MyVector(-v2.x, -v2.y);

        MyVector T1 = P + d1 * m_dist1;
        MyVector T2 = P + d2 * m_dist2;

        QPainterPath path;
        path.moveTo(T1.toPointF());
        path.lineTo(T2.toPointF());
        m_scene->setGhostPath(path, true);
    }
}

void ChamferTool::acceptDefaultDistance() {
    if (m_currentState == State_WaitingForDist1) {
        s_lastDist1 = m_dist1;
        m_currentState = State_WaitingForDist2;
        updatePrompt();
    } else if (m_currentState == State_WaitingForDist2) {
        s_lastDist2 = m_dist2;
        m_currentState = State_WaitingForFirst;
        updatePrompt();
    }
}

void ChamferTool::clearHoverPreview() {
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
    }
}

