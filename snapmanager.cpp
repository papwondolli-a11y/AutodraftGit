#include "snapmanager.h"
#include "caditem.h"  // CadItem::Type 등을 위해 필요
#include "myentity.h" // 교차점 계산을 위해 필요
#include <QDebug>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QtMath>
#include <limits>
#include <QApplication>

SnapManager::SnapManager(QObject *parent) : QObject(parent) {
    m_osnapOn = true;
    m_gridSnapOn = false;
    m_orthoOn = false;

    m_snapEndpoint = true;
    m_snapMidpoint = false;
    m_snapCenter = true;
    m_snapQuadrant = false;
    m_snapIntersection = true;
    m_snapNearest = false;
    m_snapPerpendicular = false;
}

int SnapManager::getSnapPriority(SnapType type) const {
    switch (type) {
    case SnapType::Endpoint:
        return 10;
    case SnapType::Intersection:
        return 9;
    case SnapType::Center:
        return 8;
    case SnapType::Midpoint:
        return 7;
    case SnapType::Quadrant:
        return 6;
    case SnapType::Perpendicular:
        return 5;
    case SnapType::Nearest:
        return 4;
    default:
        return 0;
    }
}

bool SnapManager::isOrthoActive() const {
    bool shiftPressed = (QApplication::keyboardModifiers() & Qt::ShiftModifier);
    return m_orthoOn != shiftPressed;
}

SnapManager::SnapResult SnapManager::snapPoint(const QPointF &mousePos,
                                               QGraphicsScene *scene,
                                               const QPointF &basePoint,
                                               bool useOrtho) {
    SnapResult result;
    result.snappedPos = mousePos;
    result.type = SnapType::None;
    result.isValid = false;

    if (m_suspended)
        return result;

    // 1. 객체 스냅 (basePoint 전달)
    if (m_osnapOn && scene) {
        SnapResult osnap = findObjectSnap(mousePos, scene, basePoint);
        if (osnap.isValid)
            return osnap;
    }

    // 2. 그리드 스냅
    if (m_gridSnapOn) {
        result.snappedPos = applyGrid(mousePos);
        result.type = SnapType::Grid;
        result.isValid = true;
    }

    // 3. 직교 모드
    bool orthoActive = (isOrthoActive() || useOrtho);
    if (orthoActive && !basePoint.isNull()) {
        result.snappedPos = applyOrtho(result.snappedPos, basePoint);
    }

    return result;
}

QPointF SnapManager::applyGrid(const QPointF &pos) {
    if (m_gridSize < 1e-9)
        return pos;
    double x = std::floor(pos.x() / m_gridSize + 0.5) * m_gridSize;
    double y = std::floor(pos.y() / m_gridSize + 0.5) * m_gridSize;
    return QPointF(x, y);
}

QPointF SnapManager::applyOrtho(const QPointF &pos, const QPointF &base) {
    double dx = qAbs(pos.x() - base.x());
    double dy = qAbs(pos.y() - base.y());
    if (dx > dy)
        return QPointF(pos.x(), base.y());
    else
        return QPointF(base.x(), pos.y());
}

SnapManager::SnapResult SnapManager::findObjectSnap(const QPointF &pos,
                                                    QGraphicsScene *scene,
                                                    const QPointF &basePoint) {
    SnapResult bestSnap;
    bestSnap.snappedPos = pos;
    bestSnap.type = SnapType::None;
    bestSnap.isValid = false;

    double actualDist = m_snapDistance;

    if (scene) {
        const QList<QGraphicsView *> views = scene->views();
        if (!views.isEmpty()) {
            double scale = views.first()->viewportTransform().m11();
            if (std::abs(scale) > 1e-9) {
                actualDist = m_snapDistance / std::abs(scale);
            }
        }
    }

    double minInfoDist = actualDist;
    MyVector mouseVec(pos);

    QRectF searchRect(pos.x() - actualDist, pos.y() - actualDist, actualDist * 2,
                      actualDist * 2);

    QList<QGraphicsItem *> items = scene->items(searchRect);

    // [1] 단일 객체 검사
    // C++11 range-loop detach 경고 방지를 위해 철저히 인덱스 기반 루프 사용
    for (int i = 0; i < items.size(); ++i) {
        QGraphicsItem *item = items[i];

        if (item->type() >= CadItem::Type) {
            CadItem *cadItem = static_cast<CadItem *>(item);

            // 1-1. 고정 포인트 (End, Mid, Center, Quad)
            QList<SnapPoint> candidates = cadItem->getSnapPoints();

            for (int j = 0; j < candidates.size(); ++j) {
                const SnapPoint &pt = candidates[j];
                if (!isModeEnabled(pt.type))
                    continue;

                double dist = std::numeric_limits<double>::max();

                if (pt.type == SnapType::Center) {
                    double distToCenterPoint = mouseVec.distanceTo(pt.pos);
                    double distToEdge = cadItem->distanceTo(mouseVec);
                    dist = std::min(distToCenterPoint, distToEdge);
                } else {
                    dist = mouseVec.distanceTo(pt.pos);
                }

                if (dist < actualDist) {
                    int p = getSnapPriority(pt.type);
                    int bestP = getSnapPriority(bestSnap.type);

                    if (p > bestP || (p == bestP && dist < minInfoDist)) {
                        minInfoDist = dist;
                        bestSnap.snappedPos = pt.pos.toPointF();
                        bestSnap.type = pt.type;
                        bestSnap.isValid = true;
                    }
                }
            }

            // 1-2. 근처점 (Nearest)
            if (m_snapNearest) {
                SnapPoint nearPt = cadItem->getNearestSnap(mouseVec);
                if (nearPt.type == SnapType::Nearest) {
                    double dist = mouseVec.distanceTo(nearPt.pos);
                    if (dist < actualDist) {
                        int p = getSnapPriority(SnapType::Nearest);
                        int bestP = getSnapPriority(bestSnap.type);

                        if (p > bestP || (p == bestP && dist < minInfoDist)) {
                            minInfoDist = dist;
                            bestSnap.snappedPos = nearPt.pos.toPointF();
                            bestSnap.type = SnapType::Nearest;
                            bestSnap.isValid = true;
                        }
                    }
                }
            }

            // 1-3. 수선의 발 (Perpendicular)
            if (m_snapPerpendicular && !basePoint.isNull()) {
                MyVector baseVec(basePoint);
                const MyEntity *entity = cadItem->getEntityData();

                // [핵심 보완 1] entity가 널이 아닐 때만 수선의 발 연산 수행
                if (entity) {
                    MyVector perPt = entity->getPerpendicularPoint(baseVec);
                    double dist = mouseVec.distanceTo(perPt);
                    if (dist < actualDist) {
                        int p = getSnapPriority(SnapType::Perpendicular);
                        int bestP = getSnapPriority(bestSnap.type);

                        if (p > bestP || (p == bestP && dist < minInfoDist)) {
                            minInfoDist = dist;
                            bestSnap.snappedPos = perPt.toPointF();
                            bestSnap.type = SnapType::Perpendicular;
                            bestSnap.isValid = true;
                        }
                    }
                }
            }
        }
    }

    // [2] 교차점 검사 (Intersection)
    if (m_snapIntersection && items.size() >= 2) {
        for (int i = 0; i < items.size(); ++i) {
            for (int j = i + 1; j < items.size(); ++j) {
                if (items[i]->type() >= CadItem::Type &&
                    items[j]->type() >= CadItem::Type) {
                    CadItem *c1 = static_cast<CadItem *>(items[i]);
                    CadItem *c2 = static_cast<CadItem *>(items[j]);

                    const MyEntity *e1 = c1->getEntityData();
                    const MyEntity *e2 = c2->getEntityData();

                    // [핵심 보완 2] 두 객체의 Entity 데이터가 모두 유효할 때만 교차점 계산 수행
                    if (!e1 || !e2) continue;

                    std::vector<MyVector> intersects = e1->intersect(e2);

                    // C++11 detach 경고 방지를 위한 인덱스 루프 적용
                    for (int k = 0; k < static_cast<int>(intersects.size()); ++k) {
                        const MyVector &ipt = intersects[k];
                        double dist = mouseVec.distanceTo(ipt);
                        if (dist < actualDist) {
                            int p = getSnapPriority(SnapType::Intersection);
                            int bestP = getSnapPriority(bestSnap.type);

                            if (p > bestP || (p == bestP && dist < minInfoDist)) {
                                minInfoDist = dist;
                                bestSnap.snappedPos = ipt.toPointF();
                                bestSnap.type = SnapType::Intersection;
                                bestSnap.isValid = true;
                            }
                        }
                    }
                }
            }
        }
    }

    return bestSnap;
}

bool SnapManager::isModeEnabled(SnapType type) const {
    switch (type) {
    case SnapType::Endpoint:
        return m_snapEndpoint;
    case SnapType::Midpoint:
        return m_snapMidpoint;
    case SnapType::Center:
        return m_snapCenter;
    case SnapType::Quadrant:
        return m_snapQuadrant;
    case SnapType::Intersection:
        return m_snapIntersection;
    case SnapType::Nearest:
        return m_snapNearest;
    case SnapType::Perpendicular:
        return m_snapPerpendicular;
    default:
        return false;
    }
}

// 토글 함수들
void SnapManager::toggleOSnap() {
    m_osnapOn = !m_osnapOn;
    emit osnapChanged(m_osnapOn);
}
void SnapManager::toggleOrtho() {
    m_orthoOn = !m_orthoOn;
    emit orthoChanged(m_orthoOn);
}
void SnapManager::toggleSnapEndpoint() {
    m_snapEndpoint = !m_snapEndpoint;
    emit snapEndpointChanged(m_snapEndpoint);
}
void SnapManager::toggleSnapMidpoint() {
    m_snapMidpoint = !m_snapMidpoint;
    emit snapMidpointChanged(m_snapMidpoint);
}
void SnapManager::toggleSnapCenter() {
    m_snapCenter = !m_snapCenter;
    emit snapCenterChanged(m_snapCenter);
}
void SnapManager::toggleSnapIntersection() {
    m_snapIntersection = !m_snapIntersection;
    emit snapIntersectionChanged(m_snapIntersection);
}
void SnapManager::toggleSnapQuadrant() {
    m_snapQuadrant = !m_snapQuadrant;
    emit snapQuadrantChanged(m_snapQuadrant);
}
void SnapManager::toggleSnapNearest() {
    m_snapNearest = !m_snapNearest;
    emit snapNearestChanged(m_snapNearest);
}
void SnapManager::toggleSnapPerpendicular() {
    m_snapPerpendicular = !m_snapPerpendicular;
    emit snapPerpendicularChanged(m_snapPerpendicular);
}

void SnapManager::clearAllSnaps() {
    if (m_snapEndpoint) {
        m_snapEndpoint = false;
        emit snapEndpointChanged(false);
    }
    if (m_snapMidpoint) {
        m_snapMidpoint = false;
        emit snapMidpointChanged(false);
    }
    if (m_snapCenter) {
        m_snapCenter = false;
        emit snapCenterChanged(false);
    }
    if (m_snapIntersection) {
        m_snapIntersection = false;
        emit snapIntersectionChanged(false);
    }
    if (m_snapQuadrant) {
        m_snapQuadrant = false;
        emit snapQuadrantChanged(false);
    }
    if (m_snapNearest) {
        m_snapNearest = false;
        emit snapNearestChanged(false);
    }
    if (m_snapPerpendicular) {
        m_snapPerpendicular = false;
        emit snapPerpendicularChanged(false);
    }
}