#include "extendtool.h"
#include "appcontext.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "gripitem.h"
#include "myarc.h"
#include "mycircle.h"
#include "mygraphicsscene.h"
#include "myline.h"
#include "mymath.h"
#include "mypolyline.h"
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QKeyEvent>
//#include <algorithm>
#include <cmath>

ExtendTool::ExtendTool(QObject *parent)
    : AbstractTool(parent), m_currentState(State_SelectingBoundaryEdges),
    m_selectAllMode(false), m_isFenceSelecting(false) {}

ExtendTool::~ExtendTool() { clearOverlay(); }

void ExtendTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene)
        return;
    AbstractTool::activate(scene);
    m_currentState = State_SelectingBoundaryEdges;
    m_boundaryEdges.clear();
    m_selectAllMode = false;
    m_isFenceSelecting = false;
    m_fencePoints.clear();
    hideGripsOfSelectedItems();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("EXTEND", CommandDock::Command);
    }
    updatePrompt();
}

void ExtendTool::deactivate() {
    clearOverlay();
    m_isFenceSelecting = false;
    m_fencePoints.clear();

    if (m_scene) {
        for (size_t i = 0; i < m_boundaryEdges.size(); ++i) {
            if (m_boundaryEdges[i])
                m_boundaryEdges[i]->setSelected(false);
        }
        m_scene->clearSelection();
    }
    m_boundaryEdges.clear();
    AbstractTool::deactivate();
}

void ExtendTool::updatePrompt() {
    if (!m_appContext)
        return;
    auto dock = m_appContext->commandDock();
    if (!dock)
        return;

    if (m_currentState == State_SelectingBoundaryEdges) {
        dock->showInteractivePrompt(
            tr("확장 경계선을 선택 하세요 ( 엔터,스페이스 : 근처객체) : "), {});
    } else {
        dock->showInteractivePrompt(tr("확장할 선을 선택 하세요 : "), {});
    }
}

void ExtendTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (m_currentState == State_SelectingBoundaryEdges) {
        handleSelectionPress(pos, button);
    } else {
        // 마우스 우클릭 시 가상 펜스 모드 취소
        if (button == Qt::RightButton) {
            if (m_isFenceSelecting) {
                m_isFenceSelecting = false;
                m_fencePoints.clear();
                clearHoverPreview();
            } else {
                emit toolFinished();
            }
            return;
        }

        if (button != Qt::LeftButton)
            return;

        if (!m_isFenceSelecting) {
            QGraphicsItem *item = itemAtPos(pos);
            CadItem *cadItem = dynamic_cast<CadItem *>(item);
            if (cadItem) {
                clearHoverPreview();
                tryExtend(cadItem, pos);
            } else {
                // 객체가 없는 빈 공간 클릭 -> 연속 펜스 모드 시작
                m_appContext->commandDock()->showInteractivePrompt(tr("가상선택선(종료:우클릭):"), {});
                m_isFenceSelecting = true;
                m_fencePoints.clear();
                m_fencePoints.push_back(pos);
                clearHoverPreview();
            }
        } else {
            // 펜스 진행 중 추가 클릭 -> 선분에 교차하는 객체 연장 실행
            m_fencePoints.push_back(pos);
            size_t n = m_fencePoints.size();
            processFenceSegment(m_fencePoints[n - 2], m_fencePoints[n - 1]);
        }
    }
}

void ExtendTool::mouseMove(QPointF pos) {
    if (m_currentState == State_SelectingExtendTarget) {
        if (m_isFenceSelecting && !m_fencePoints.empty()) {
            // 시작점부터 현재 마우스 위치까지 펜스 가상선 시각화
            QPainterPath path;
            path.moveTo(m_fencePoints[0]);
            for (size_t i = 1; i < m_fencePoints.size(); ++i) {
                path.lineTo(m_fencePoints[i]);
            }
            path.lineTo(pos);
            if (m_scene) {
                m_scene->setGhostPath(path, true);
            }
        } else {
            updateHoverPreview(pos);
        }
    }
}

void ExtendTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
    Q_UNUSED(pos);
    Q_UNUSED(button);
}

void ExtendTool::handleSelectionPress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene || button != Qt::LeftButton)
        return;

    QGraphicsItem *item = itemAtPos(pos);
    CadItem *cadItem = dynamic_cast<CadItem *>(item);

    // 단일 선택만 가능하도록 기존 선택 해제 후 추가
    if (cadItem) {
        for (size_t i = 0; i < m_boundaryEdges.size(); ++i) {
            if (m_boundaryEdges[i])
                m_boundaryEdges[i]->setSelected(false);
        }
        m_boundaryEdges.clear();
        m_boundaryEdges.push_back(cadItem);
        cadItem->setSelected(true);
        hideGripsOfSelectedItems();

        // 1개 객체가 선택되면 자동으로 다음 상태로 넘어가도록 처리
        m_currentState = State_SelectingExtendTarget;
        updatePrompt();
    }
}

void ExtendTool::processFenceSegment(const QPointF &p1, const QPointF &p2) {
    if (!m_scene) return;
    MyLine fenceLine((MyVector(p1)), MyVector(p2));

    QList<QGraphicsItem *> items = m_scene->items();

    // Range-loop detach 방지 인덱스 루프 사용
    for (int i = 0; i < items.size(); ++i) {
        CadItem *cadItem = dynamic_cast<CadItem *>(items[i]);
        if (!cadItem) continue;

        const MyEntity *entity = cadItem->getEntityData();
        if (!entity) continue;

        std::vector<MyVector> intersections = entity->intersect(&fenceLine);
        if (!intersections.empty()) {
            // 가상선이 객체와 교차하는 위치를 "클릭 위치"로 간주하여 연장 시도
            tryExtend(cadItem, intersections[0].toPointF());
        }
    }
}

void ExtendTool::clearOverlay() {
    if (m_scene) {
        m_scene->clearOverlay();
    }
}

void ExtendTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Space) {
        if (m_currentState == State_SelectingBoundaryEdges) {
            if (m_boundaryEdges.empty())
                m_selectAllMode = true;
            m_currentState = State_SelectingExtendTarget;
            updatePrompt();
        } else {
            if (m_isFenceSelecting) {
                m_isFenceSelecting = false;
                m_fencePoints.clear();
                clearHoverPreview();
            } else {
                emit toolFinished();
            }
        }
    } else if (event->key() == Qt::Key_Escape) {
        if (m_isFenceSelecting) {
            m_isFenceSelecting = false;
            m_fencePoints.clear();
            clearHoverPreview();
        } else {
            emit toolFinished();
        }
    } else {
        AbstractTool::keyPress(event);
    }
}

bool ExtendTool::processCommand(const QString &command) {
    if (command.isEmpty() && m_currentState == State_SelectingBoundaryEdges) {
        if (m_boundaryEdges.empty())
            m_selectAllMode = true;
        m_currentState = State_SelectingExtendTarget;
        updatePrompt();
        return true;
    }
    return false;
}

void ExtendTool::tryExtend(CadItem *targetItem, const QPointF &clickPos) {
    bool isFirstEnd = false;
    std::vector<MyVector> intersections =
        findAllExtendedIntersections(targetItem, clickPos, isFirstEnd);
    if (intersections.empty())
        return;

    const MyEntity *entity = targetItem->getEntityData();
    MyVector endPt;
    if (entity->type() == EntityType::Line) {
        const MyLine *line = static_cast<const MyLine *>(entity);
        endPt = isFirstEnd ? line->start : line->end;
    } else if (entity->type() == EntityType::Arc) {
        const MyArc *arc = static_cast<const MyArc *>(entity);
        double angle =
            isFirstEnd ? arc->startAngle : (arc->startAngle + arc->spanAngle);
        double rad = MyMath::deg2rad(angle);
        endPt = arc->center +
                MyVector(std::cos(rad) * arc->radius, std::sin(rad) * arc->radius);
    } else if (entity->type() == EntityType::Polyline) {
        const MyPolyline *pline = static_cast<const MyPolyline *>(entity);
        if (pline->vertices.empty())
            return;
        endPt = isFirstEnd ? pline->vertices.front() : pline->vertices.back();
    }

    MyVector nearest;
    double minDist = 1e18;
    bool found = false;
    for (size_t i = 0; i < intersections.size(); ++i) {
        double d = intersections[i].distanceTo(endPt);
        if (d < minDist) {
            minDist = d;
            nearest = intersections[i];
            found = true;
        }
    }

    if (found) {
        executeExtend(targetItem, endPt.toPointF(), nearest.toPointF(), isFirstEnd);
    }
}

void ExtendTool::executeExtend(CadItem *targetItem, const QPointF &oldEnd,
                               const QPointF &newEnd, bool isFirstEnd) {
    QUndoStack *stack = m_appContext ? m_appContext->undoStack() : nullptr;
    if (stack) {
        stack->push(
            new ExtendEntityCommand(targetItem, oldEnd, newEnd, isFirstEnd));
    } else {
        targetItem->updateEndpoint(newEnd, isFirstEnd);
    }
}

std::vector<MyVector> ExtendTool::findAllExtendedIntersections(
    CadItem *targetItem, const QPointF &clickPos, bool &isFirstEnd) {
    std::vector<MyVector> results;
    const MyEntity *targetEntity = targetItem->getEntityData();
    if (!targetEntity)
        return results;

    if (targetEntity->type() == EntityType::Line) {
        const MyLine *line = static_cast<const MyLine *>(targetEntity);
        isFirstEnd = (MyVector(clickPos).distanceTo(line->start) <
                      MyVector(clickPos).distanceTo(line->end));
    } else if (targetEntity->type() == EntityType::Arc) {
        const MyArc *arc = static_cast<const MyArc *>(targetEntity);
        double sRad = MyMath::deg2rad(arc->startAngle);
        double eRad = MyMath::deg2rad(arc->startAngle + arc->spanAngle);
        MyVector p1 = arc->center + MyVector(std::cos(sRad) * arc->radius,
                                             std::sin(sRad) * arc->radius);
        MyVector p2 = arc->center + MyVector(std::cos(eRad) * arc->radius,
                                             std::sin(eRad) * arc->radius);
        isFirstEnd =
            (MyVector(clickPos).distanceTo(p1) < MyVector(clickPos).distanceTo(p2));
    } else if (targetEntity->type() == EntityType::Polyline) {
        const MyPolyline *pline = static_cast<const MyPolyline *>(targetEntity);
        if (pline->vertices.size() < 2)
            return results;
        isFirstEnd = (MyVector(clickPos).distanceTo(pline->vertices.front()) <
                      MyVector(clickPos).distanceTo(pline->vertices.back()));
    }

    std::vector<CadItem *> boundaries;
    if (m_selectAllMode) {
        QList<QGraphicsItem *> items = m_scene->items();
        for (int i = 0; i < items.size(); ++i) {
            CadItem *ci = dynamic_cast<CadItem *>(items[i]);
            if (ci && ci != targetItem)
                boundaries.push_back(ci);
        }
    } else {
        for (size_t i = 0; i < m_boundaryEdges.size(); ++i) {
            CadItem *edge = m_boundaryEdges[i];
            if (edge && edge != targetItem)
                boundaries.push_back(edge);
        }
    }

    for (size_t i = 0; i < boundaries.size(); ++i) {
        CadItem *boundary = boundaries[i];
        const MyEntity *bEntity = boundary->getEntityData();
        if (!bEntity)
            continue;

        if (targetEntity->type() == EntityType::Line) {
            const MyLine *line = static_cast<const MyLine *>(targetEntity);
            MyVector dir =
                isFirstEnd ? (line->start - line->end) : (line->end - line->start);
            MyVector base = isFirstEnd ? line->start : line->end;
            MyLine ray(base, base + dir * 1e6);
            std::vector<MyVector> pts = ray.intersect(bEntity);
            for (size_t j = 0; j < pts.size(); ++j) {
                if (MyMath::dotProduct(pts[j] - base, dir) > 0)
                    results.push_back(pts[j]);
            }
        } else if (targetEntity->type() == EntityType::Arc) {
            const MyArc *arc = static_cast<const MyArc *>(targetEntity);
            MyCircle circle(arc->center, arc->radius);
            std::vector<MyVector> pts = circle.intersect(bEntity);
            for (size_t j = 0; j < pts.size(); ++j) {
                double angle = MyMath::rad2deg(
                    std::atan2(pts[j].y - arc->center.y, pts[j].x - arc->center.x));
                angle = MyMath::normalizeAngle(angle);
                if (!MyMath::isAngleBetween(angle, arc->startAngle, arc->spanAngle))
                    results.push_back(pts[j]);
            }
        } else if (targetEntity->type() == EntityType::Polyline) {
            const MyPolyline *pline = static_cast<const MyPolyline *>(targetEntity);
            MyVector v1, v2;
            if (isFirstEnd) {
                v1 = pline->vertices[1];
                v2 = pline->vertices[0];
            } else {
                v1 = pline->vertices[pline->vertices.size() - 2];
                v2 = pline->vertices.back();
            }
            MyVector dir = v2 - v1;
            MyLine ray(v2, v2 + dir * 1e6);
            std::vector<MyVector> pts = ray.intersect(bEntity);
            for (size_t j = 0; j < pts.size(); ++j) {
                if (MyMath::dotProduct(pts[j] - v2, dir) > 0)
                    results.push_back(pts[j]);
            }
        }
    }
    return results;
}

void ExtendTool::updateHoverPreview(const QPointF &pos) {
    if (!m_scene)
        return;
    QGraphicsItem *item = itemAtPos(pos);
    CadItem *targetItem = dynamic_cast<CadItem *>(item);
    if (!targetItem) {
        clearHoverPreview();
        return;
    }

    bool isFirstEnd = false;
    std::vector<MyVector> intersections =
        findAllExtendedIntersections(targetItem, pos, isFirstEnd);
    if (intersections.empty()) {
        clearHoverPreview();
        return;
    }

    const MyEntity *entity = targetItem->getEntityData();
    MyVector startPt;
    if (entity->type() == EntityType::Line) {
        const MyLine *line = static_cast<const MyLine *>(entity);
        startPt = isFirstEnd ? line->start : line->end;
    } else if (entity->type() == EntityType::Arc) {
        const MyArc *arc = static_cast<const MyArc *>(entity);
        double ang =
            isFirstEnd ? arc->startAngle : (arc->startAngle + arc->spanAngle);
        startPt =
            arc->center + MyVector(std::cos(MyMath::deg2rad(ang)) * arc->radius,
                                   std::sin(MyMath::deg2rad(ang)) * arc->radius);
    } else if (entity->type() == EntityType::Polyline) {
        const MyPolyline *pline = static_cast<const MyPolyline *>(entity);
        startPt = isFirstEnd ? pline->vertices.front() : pline->vertices.back();
    }

    MyVector nearest;
    double minDist = 1e18;
    for (size_t i = 0; i < intersections.size(); ++i) {
        double d = intersections[i].distanceTo(startPt);
        if (d < minDist) {
            minDist = d;
            nearest = intersections[i];
        }
    }

    QPainterPath path;
    if (entity->type() == EntityType::Line ||
        entity->type() == EntityType::Polyline) {
        path.moveTo(startPt.toPointF());
        path.lineTo(nearest.toPointF());
    } else if (entity->type() == EntityType::Arc) {
        const MyArc *arc = static_cast<const MyArc *>(entity);
        double a1 =
            isFirstEnd ? arc->startAngle : (arc->startAngle + arc->spanAngle);
        double a2 = MyMath::rad2deg(
            std::atan2(nearest.y - arc->center.y, nearest.x - arc->center.x));
        double span = MyMath::normalizeAngle(a2 - a1);

        if (arc->spanAngle < 0 && !isFirstEnd)
            span -= 360.0;
        if (arc->spanAngle > 0 && isFirstEnd)
            span -= 360.0;

        QRectF rect(arc->center.x - arc->radius, arc->center.y - arc->radius,
                    arc->radius * 2, arc->radius * 2);
        path.arcMoveTo(rect, -a1);
        path.arcTo(rect, -a1, -span);
    }

    m_scene->setGhostPath(path, true);
}

void ExtendTool::clearHoverPreview() {
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
    }
}