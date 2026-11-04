#include "severtool.h"
#include "appcontext.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "myarc.h"
#include "mycircle.h"
#include "mygraphicsscene.h"
#include "myline.h"
#include "mymath.h"
#include "mypolyline.h"
#include "multilinetagutil.h"
#include <QApplication>
#include <QKeyEvent>
#include <QUndoStack>
#include <algorithm>

SeverTool::SeverTool(QObject *parent)
    : AbstractTool(parent), m_currentState(State_SelectingFirstPoint), m_targetItem(nullptr) {}

SeverTool::~SeverTool() {
    clearOverlay();
}

void SeverTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene)
        return;
    AbstractTool::activate(scene);
    m_currentState = State_SelectingFirstPoint;
    m_targetItem = nullptr;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("BREAK", CommandDock::Command);
    }
    updatePrompt();
}

void SeverTool::deactivate() {
    clearOverlay();
    AbstractTool::deactivate();
}

void SeverTool::updatePrompt() {
    if (!m_appContext)
        return;
//    auto dock = m_appContext->commandDock();
    IAppContext *ctx = getContext();

    // if (!dock)
    //     return;

    if (ctx && ctx->commandDock()) {
        if (m_currentState == State_SelectingFirstPoint) {
            ctx->commandDock()->setToolActive(true);
            ctx->commandDock()->logMessage("BREAK", CommandDock::Command);
            ctx->commandDock()->showInteractivePrompt(tr(" 첫번째 점을 지정하세요 (객체 선택) : "), {});
        } else if (m_currentState == State_SelectingSecondPoint) {
            ctx->commandDock()->showInteractivePrompt(tr(" 두번째 점을 지정하세요 : "), {});
        } else if (m_currentState == State_WaitingForDistance) {
            ctx->commandDock()->showInteractivePrompt(tr(" 거리를 입력하세요 : "), {});
        }
    }
}

void SeverTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (button == Qt::RightButton) {
        emit toolFinished();
        return;
    }

    if (button != Qt::LeftButton)
        return;

    if (m_currentState == State_SelectingFirstPoint) {
        QGraphicsItem *item = itemAtPos(pos);
        m_targetItem = dynamic_cast<CadItem *>(item);

        if (m_targetItem) {
            m_p1 = m_targetItem->getNearestSnap(MyVector(pos)).pos;
            m_currentState = State_SelectingSecondPoint;
            updatePrompt();
        }
    } else if (m_currentState == State_SelectingSecondPoint) {
        if (!m_targetItem) { emit toolFinished(); return; }

        Qt::KeyboardModifiers mods = QApplication::queryKeyboardModifiers();
        if (mods & Qt::ShiftModifier) {
            // Shift+클릭 → 방향점 고정, 거리 입력 대기 상태로 전환
            m_dirPoint = MyVector(pos);
            m_currentState = State_WaitingForDistance;
            clearOverlay();
            updatePrompt();
        } else {
            // 일반 클릭 → 기존 동작: 클릭점 자체가 p2
            m_p2 = m_targetItem->getNearestSnap(MyVector(pos)).pos;
            executeBreak();
            emit toolFinished();
        }
    } else if (m_currentState == State_WaitingForDistance) {
        if (!m_targetItem) { emit toolFinished(); return; }
        // 거리 입력 대기 중 클릭 → 방향점 업데이트
        m_dirPoint = MyVector(pos);
        updatePrompt();
    }
}

void SeverTool::mouseMove(QPointF pos) {
    m_currentMousePos = pos;
    if (!m_scene) return;

    if (m_currentState == State_SelectingSecondPoint) {
        // 러버밴드: p1 → 마우스
        QPainterPath path;
        path.moveTo(m_p1.toPointF());
        path.lineTo(pos);
        m_scene->setGhostPath(path, true, Qt::cyan);
    } else if (m_currentState == State_WaitingForDistance) {
        // 방향선: p1 → 방향점 (노란색으로 고정 방향 표시)
        QPainterPath path;
        path.moveTo(m_p1.toPointF());
        path.lineTo(m_dirPoint.toPointF());
        m_scene->setGhostPath(path, true, QColor(255, 200, 0));
    }
}

void SeverTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Space) {
        if (m_currentState == State_SelectingSecondPoint) {
            // 제자리 끊기 (p1 == p2)
            m_p2 = m_p1;
            executeBreak();
            emit toolFinished();
        } else if (m_currentState == State_WaitingForDistance) {
            // Enter → 방향점 자체를 p2로 사용 (Shift+클릭점으로 끊기)
            m_p2 = m_targetItem
                       ? m_targetItem->getNearestSnap(m_dirPoint).pos
                       : m_dirPoint;
            executeBreak();
            emit toolFinished();
        }
    } else if (event->key() == Qt::Key_Escape) {
        if (m_currentState == State_WaitingForDistance) {
            // ESC → 두번째점 선택으로 복귀
            m_currentState = State_SelectingSecondPoint;
            updatePrompt();
        } else {
            emit toolFinished();
        }
    } else {
        AbstractTool::keyPress(event);
    }
}

void SeverTool::clearOverlay() {
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
    }
}

void SeverTool::executeBreak() {
    if (!m_targetItem || !m_scene)
        return;

    const MyEntity *entity = m_targetItem->getEntityData();
    if (!entity)
        return;

    IAppContext *ctx = getContext();
    if (!ctx)
        return;

    auto factory = ctx->entityFactory();
    auto undoStack = ctx->undoStack();
    auto curPen = m_targetItem->pen();
    QString layerName = m_targetItem->layerName();

    if (undoStack) {
        undoStack->beginMacro(tr("끊기 (Break)"));
        undoStack->push(new RemoveEntityCommand(m_scene, m_targetItem));
    }

    double t1 = getParam(entity, m_p1);
    double t2 = getParam(entity, m_p2);

    // t1, t2 순서 정렬 (원/호 제외)
    bool isCircle = (entity->type() == EntityType::Circle);
    if (!isCircle && t1 > t2) {
        std::swap(t1, t2);
    }

    QList<QGraphicsItem *> newItems;

    if (entity->type() == EntityType::Line) {
        const MyLine *line = static_cast<const MyLine *>(entity);
        // [0, t1] 구간
        if (t1 > 0.001) {
            MyVector start = line->start;
            MyVector end = line->start + (line->end - line->start) * t1;
            newItems.append(factory->createLine(QLineF(start.toPointF(), end.toPointF()), curPen));
        }
        // [t2, 1] 구간
        if (t2 < 0.999) {
            MyVector start = line->start + (line->end - line->start) * t2;
            MyVector end = line->end;
            newItems.append(factory->createLine(QLineF(start.toPointF(), end.toPointF()), curPen));
        }
    } else if (isCircle) {
        const MyCircle *c = static_cast<const MyCircle *>(entity);
        // 원은 끊으면 무조건 호가 됨. p1 == p2면 360도 호.
        // p1 != p2면 p2에서 p1까지 CCW 방향의 호 (p1~p2 사이가 제거됨)
        double a1 = MyMath::normalizeAngle(MyMath::rad2deg(std::atan2(m_p1.y - c->center.y, m_p1.x - c->center.x)));
        double a2 = MyMath::normalizeAngle(MyMath::rad2deg(std::atan2(m_p2.y - c->center.y, m_p2.x - c->center.x)));

        if (std::abs(a1 - a2) < 0.001) {
            // 제자리 끊기: 360도 호 혹은 그냥 원으로 유지 (여기서는 호로 변환)
            newItems.append(factory->createArc(c->center.toPointF(), c->radius, a1, 360.0, curPen));
        } else {
            double span = MyMath::normalizeAngle(a1 - a2);
            if (span < 0.001) span = 360.0;
            newItems.append(factory->createArc(c->center.toPointF(), c->radius, a2, span, curPen));
        }
    } else if (entity->type() == EntityType::Arc) {
        const MyArc *arc = static_cast<const MyArc *>(entity);
        // [0, t1] 구간
        if (t1 > 0.001) {
            double s = arc->startAngle;
            double sp = arc->spanAngle * t1;
            newItems.append(factory->createArc(arc->center.toPointF(), arc->radius, s, sp, curPen));
        }
        // [t2, 1] 구간
        if (t2 < 0.999) {
            double s = arc->startAngle + arc->spanAngle * t2;
            double sp = arc->spanAngle * (1.0 - t2);
            newItems.append(factory->createArc(arc->center.toPointF(), arc->radius, s, sp, curPen));
        }
    } else if (entity->type() == EntityType::Polyline) {
        const MyPolyline *pline = static_cast<const MyPolyline *>(entity);
        size_t n = pline->isClosed ? pline->vertices.size() : pline->vertices.size() - 1;
        
        // [0, t1] 폴리라인
        if (t1 > 0.001) {
            std::vector<MyVector> v1;
            double p1 = t1 * n;
            int idx1 = std::floor(p1 + 0.001);
            if (idx1 >= (int)pline->vertices.size()) idx1 = pline->vertices.size() - 1;

            for (int i = 0; i <= idx1; ++i) v1.push_back(pline->vertices[i]);
            
            double localT = p1 - idx1;
            if (localT > 0.001 && idx1 < (int)n) {
                MyVector va = pline->vertices[idx1];
                MyVector vb = pline->vertices[(idx1 + 1) % pline->vertices.size()];
                v1.push_back(va + (vb - va) * localT);
            }
            if (v1.size() >= 2) newItems.append(factory->createPolyline(v1, false, curPen));
        }

        // [t2, 1] 폴리라인
        if (t2 < 0.999) {
            std::vector<MyVector> v2;
            double p2 = t2 * n;
            int idx2 = std::floor(p2 + 0.001);
            if (idx2 >= (int)pline->vertices.size()) idx2 = pline->vertices.size() - 1;

            double localT = p2 - idx2;
            if (idx2 < (int)n) {
                MyVector va = pline->vertices[idx2];
                MyVector vb = pline->vertices[(idx2 + 1) % pline->vertices.size()];
                v2.push_back(va + (vb - va) * localT);
            }
            for (int i = idx2 + 1; i < (int)pline->vertices.size(); ++i) v2.push_back(pline->vertices[i]);
            
            if (v2.size() >= 2) newItems.append(factory->createPolyline(v2, false, curPen));
        }
    }

    // 인덱스 기반 루프로 새로운 아이템들 추가
    for (int i = 0; i < newItems.size(); ++i) {
        QGraphicsItem *item = newItems.at(i);
        if (CadItem *ci = dynamic_cast<CadItem *>(item)) {
            ci->setLayerName(layerName);
            // 다중선 태그 보존 (끊긴 조각 모두 원본 그룹/역할 유지)
            MultilineTagUtil::copyTags(m_targetItem, ci);
            if (undoStack) {
                undoStack->push(new AddEntityCommand(m_scene, ci));
            } else {
                m_scene->addItem(ci);
                m_scene->reparentItemToLayer(ci, layerName);
            }
        }
    }

    if (undoStack) {
        undoStack->endMacro();
    } else {
        m_scene->removeItem(m_targetItem);
        delete m_targetItem;
    }
}

bool SeverTool::processCommand(const QString &command) {
    QString cmd = command.trimmed();

    // ── Shift+클릭 후 거리 입력 상태 ──────────────────────────
    if (m_currentState == State_WaitingForDistance) {
        if (cmd.isEmpty()) {
            // 빈 Enter → 방향점 자체가 p2
            if (m_targetItem)
                m_p2 = m_targetItem->getNearestSnap(m_dirPoint).pos;
            else
                m_p2 = m_dirPoint;
            executeBreak();
            emit toolFinished();
            return true;
        }
        bool ok;
        double dist = cmd.toDouble(&ok);
        if (ok && dist > 0.0 && m_targetItem) {
            const MyEntity *entity = m_targetItem->getEntityData();
            if (entity) {
                // 방향점을 기준으로 p2 계산
                m_p2 = computeP2FromDistance(entity, m_p1, dist,
                                             m_dirPoint.toPointF());
                executeBreak();
                emit toolFinished();
                return true;
            }
        }
        return false;
    }

    // ── 일반 두번째점 입력 상태 (마우스 방향 기준) ─────────────
    if (m_currentState == State_SelectingSecondPoint) {
        if (cmd.isEmpty())
            return false; // keyPress의 제자리 끊기로 처리
        bool ok;
        double dist = cmd.toDouble(&ok);
        if (ok && dist > 0.0 && m_targetItem) {
            const MyEntity *entity = m_targetItem->getEntityData();
            if (entity) {
                m_p2 = computeP2FromDistance(entity, m_p1, dist,
                                             m_currentMousePos);
                executeBreak();
                emit toolFinished();
                return true;
            }
        }
        return false;
    }

    return false;
}

// 객체의 p1 위치에서 마우스 방향으로 dist만큼 이동한 p2를 계산
// Line  : 선분 방향벡터 사용
// Arc/Circle : 호 길이 = dist, 정확한 호 위 좌표 계산
// Polyline   : p1이 속한 세그먼트 방향 사용
MyVector SeverTool::computeP2FromDistance(const MyEntity *entity,
                                          const MyVector &p1, double dist,
                                          const QPointF &mousePos) {
    MyVector toMouse = MyVector(mousePos) - p1;

    if (entity->type() == EntityType::Line) {
        const MyLine *line = static_cast<const MyLine *>(entity);
        MyVector dir = (line->end - line->start).normalized();
        // 마우스 방향과 반대면 뒤집기
        if (MyMath::dotProduct(toMouse, dir) < 0.0)
            dir = dir * -1.0;
        return p1 + dir * dist;

    } else if (entity->type() == EntityType::Arc || entity->type() == EntityType::Circle) {
        MyVector center;
        double radius;
        if (entity->type() == EntityType::Circle) {
            const MyCircle *c = static_cast<const MyCircle *>(entity);
            center = c->center;
            radius = c->radius;
        } else {
            const MyArc *a = static_cast<const MyArc *>(entity);
            center = a->center;
            radius = a->radius;
        }
        // p1의 각도 (라디안)
        double angle1 = std::atan2(p1.y - center.y, p1.x - center.x);
        // p1에서의 CCW 접선벡터: (-sin θ, cos θ)
        MyVector ccwTangent(-std::sin(angle1), std::cos(angle1));
        // 마우스가 CCW 방향이면 +, CW 방향이면 -
        double sign = (MyMath::dotProduct(toMouse, ccwTangent) >= 0.0) ? 1.0 : -1.0;
        // 호 각도 증분 = 호 길이 / 반지름
        double angleDelta = sign * dist / radius;
        double angle2 = angle1 + angleDelta;
        return MyVector(center.x + radius * std::cos(angle2),
                        center.y + radius * std::sin(angle2));

    } else if (entity->type() == EntityType::Polyline) {
        const MyPolyline *pline = static_cast<const MyPolyline *>(entity);
        size_t n = pline->isClosed ? pline->vertices.size()
                                   : pline->vertices.size() - 1;
        for (size_t i = 0; i < n; ++i) {
            const MyVector &va = pline->vertices[i];
            const MyVector &vb = pline->vertices[(i + 1) % pline->vertices.size()];
            MyLine seg(va, vb);
            MyVector near = MyMath::getNearestPointOnSegment(p1, seg);
            if (near.distanceTo(p1) < 1.0) {
                MyVector dir = (vb - va).normalized();
                if (MyMath::dotProduct(toMouse, dir) < 0.0)
                    dir = dir * -1.0;
                MyVector candidate = p1 + dir * dist;
                return m_targetItem->getNearestSnap(candidate).pos;
            }
        }
    }
    return p1;
}

double SeverTool::getParam(const MyEntity *entity, const MyVector &pt) {
    if (entity->type() == EntityType::Line) {
        const MyLine *line = static_cast<const MyLine *>(entity);
        MyVector ab = line->end - line->start;
        MyVector ap = pt - line->start;
        double lenSq = MyMath::dotProduct(ab, ab);
        if (lenSq < 0.001) return 0.0;
        return std::max(0.0, std::min(1.0, MyMath::dotProduct(ap, ab) / lenSq));
    } else if (entity->type() == EntityType::Arc || entity->type() == EntityType::Circle) {
        double startAngle, spanAngle;
        MyVector center;
        if (entity->type() == EntityType::Circle) {
            const MyCircle *c = static_cast<const MyCircle *>(entity);
            center = c->center; startAngle = 0; spanAngle = 360;
        } else {
            const MyArc *a = static_cast<const MyArc *>(entity);
            center = a->center; startAngle = a->startAngle; spanAngle = a->spanAngle;
        }
        double ang = MyMath::normalizeAngle(MyMath::rad2deg(std::atan2(pt.y - center.y, pt.x - center.x)));
        double offset = MyMath::normalizeAngle(ang - startAngle);
        if (std::abs(spanAngle) < 0.001) return 0.0;
        return offset / std::abs(spanAngle);
    } else if (entity->type() == EntityType::Polyline) {
        const MyPolyline *pline = static_cast<const MyPolyline *>(entity);
        size_t n = pline->isClosed ? pline->vertices.size() : pline->vertices.size() - 1;
        if (n == 0) return 0.0;
        for (size_t i = 0; i < n; ++i) {
            MyLine seg(pline->vertices[i], pline->vertices[(i + 1) % pline->vertices.size()]);
            MyVector nearPt = MyMath::getNearestPointOnSegment(pt, seg);
            if (nearPt.distanceTo(pt) < 1.0) {
                MyVector ab = seg.end - seg.start;
                MyVector ap = nearPt - seg.start;
                double lenSq = MyMath::dotProduct(ab, ab);
                double localT = (lenSq < 0.001) ? 0.0 : MyMath::dotProduct(ap, ab) / lenSq;
                return (double(i) + std::max(0.0, std::min(1.0, localT))) / double(n);
            }
        }
    }
    return 0.0;
}
