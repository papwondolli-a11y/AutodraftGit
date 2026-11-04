#include "trimtool.h"
#include "appcontext.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "gripitem.h"
#include "lineitem.h"
#include "multilinedata.h"
#include "myarc.h"
#include "mycircle.h"
#include "mygraphicsscene.h"
#include "myline.h"
#include "mymath.h"
#include "mypolyline.h"
#include "myspline.h"
#include <QGraphicsItem>
#include <QKeyEvent>
#include <algorithm>
#include <utility> // std::pair 사용을 위해 추가

TrimTool::TrimTool(QObject *parent)
    : AbstractTool(parent), m_currentState(State_SelectingCuttingEdges),
    m_isBoxSelecting(false), m_selectAllMode(false), m_isFenceSelecting(false) {}

TrimTool::~TrimTool() { clearOverlay(); }

void TrimTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene)
        return;
    AbstractTool::activate(scene);
    m_currentState = State_SelectingCuttingEdges;
    m_cuttingEdges.clear();
    m_selectAllMode = false;
    m_isBoxSelecting = false;
    m_isFenceSelecting = false;
    hideGripsOfSelectedItems();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("TRIM", CommandDock::Command);
    }
    updatePrompt();
}

void TrimTool::deactivate() {
    clearOverlay();
    m_isBoxSelecting = false;
    m_isFenceSelecting = false;

    if (m_scene) {
        for (size_t i = 0; i < m_cuttingEdges.size(); ++i) {
            if (m_cuttingEdges[i])
                m_cuttingEdges[i]->setSelected(false);
        }
        m_scene->clearSelection();
    }
    m_cuttingEdges.clear();
    AbstractTool::deactivate();
}

void TrimTool::updatePrompt() {
    if (!m_appContext)
        return;
    auto dock = m_appContext->commandDock();
    if (!dock)
        return;

    if (m_currentState == State_SelectingCuttingEdges ) {
        dock->showInteractivePrompt(tr("절단면 객체를 선택 하세요 ( 엔터시 모든 객체가 절단면으로 선택 ):"), {});
    } else {
        dock->showInteractivePrompt(tr("절단될 객체를 선택 하세요 <종료: 엔터,스페이스> :"), {});
    }
}

void TrimTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (m_currentState == State_SelectingCuttingEdges) {
        m_appContext->commandDock()->showInteractivePrompt(tr("절단면 객체를 선택 하세요 < 선택 종료: 엔터, 스페이스> :"), {});
        handleSelectionPress(pos, button);
    } else {
        if (button == Qt::RightButton) {
            if (m_isFenceSelecting) {
                m_isFenceSelecting = false;
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
                tryTrim(cadItem, pos);
            } else {
                m_appContext->commandDock()->showInteractivePrompt(tr("가상선택선(종료:우클릭):"), {});
                m_isFenceSelecting = true;
                m_lastFencePoint = pos;
                clearHoverPreview();
            }
        } else {
            processFenceSegment(m_lastFencePoint, pos);
            m_lastFencePoint = pos;
        }
    }
}

void TrimTool::mouseMove(QPointF pos) {
    if (m_isBoxSelecting) {
        handleSelectionMove(pos);
    } else if (m_currentState == State_SelectingTrimSegment) {
        if (m_isFenceSelecting) {
            QPainterPath path;
            path.moveTo(m_lastFencePoint);
            path.lineTo(pos);
            if (m_scene) {
                m_scene->setGhostPath(path, true);
            }
        } else {
            updateHoverPreview(pos);
        }
    }
}

void TrimTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
    Q_UNUSED(pos);
    Q_UNUSED(button);
}

void TrimTool::clearOverlay() {
    if (m_scene) {
        m_scene->clearOverlay();
    }
}

void TrimTool::handleSelectionPress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene || button != Qt::LeftButton)
        return;

    if (!m_isBoxSelecting) {
        QGraphicsItem *item = itemAtPos(pos);
        CadItem *cadItem = dynamic_cast<CadItem *>(item);

        if (cadItem) {
            auto it = std::find(m_cuttingEdges.begin(), m_cuttingEdges.end(), cadItem);
            if (it == m_cuttingEdges.end()) {
                m_cuttingEdges.push_back(cadItem);
                cadItem->setSelected(true);
                hideGripsOfSelectedItems();
            } else {
                m_cuttingEdges.erase(it);
                cadItem->setSelected(false);
            }
        } else {
            m_isBoxSelecting = true;
            m_firstCorner = pos;
            clearOverlay();
        }
    } else {
        processBoxSelection(pos);
    }
}

void TrimTool::handleSelectionMove(QPointF pos) {
    if (!m_isBoxSelecting || !m_scene)
        return;

    bool isCrossing = (pos.x() < m_firstCorner.x());
    MyGraphicsScene::SelectionMode mode = isCrossing
                                              ? MyGraphicsScene::Mode_Crossing
                                              : MyGraphicsScene::Mode_Window;
    m_scene->setSelectionRect(m_firstCorner, pos, mode);
}

void TrimTool::processBoxSelection(QPointF p2) {
    m_isBoxSelecting = false;
    clearOverlay();
    if (!m_scene) return;

    bool isWindow = (p2.x() > m_firstCorner.x());
    QList<CadItem *> topItems = isWindow
                                    ? collectItemsInWindowRect(m_firstCorner, p2)
                                    : collectItemsInCrossingRect(m_firstCorner, p2);

    for (int i = 0; i < topItems.size(); ++i) {
        CadItem *ci = topItems[i];
        auto it = std::find(m_cuttingEdges.begin(), m_cuttingEdges.end(), ci);
        if (it == m_cuttingEdges.end()) {
            m_cuttingEdges.push_back(ci);
            ci->setSelected(true);
        }
    }
    hideGripsOfSelectedItems();
}

void TrimTool::processFenceSegment(const QPointF &p1, const QPointF &p2) {
    if (!m_scene) return;
    MyLine fenceLine((MyVector(p1)), MyVector(p2));

    bool trimmedAny = false;
    do {
        trimmedAny = false;
        QList<QGraphicsItem *> items = m_scene->items();

        for (int i = 0; i < items.size(); ++i) {
            CadItem *cadItem = dynamic_cast<CadItem *>(items[i]);
            if (!cadItem) continue;

            const MyEntity *entity = cadItem->getEntityData();
            if (!entity) continue;

            std::vector<MyVector> intersections = entity->intersect(&fenceLine);
            if (!intersections.empty()) {
                bool trimmedThisItem = false;

                for (size_t k = 0; k < intersections.size(); ++k) {
                    if (tryTrim(cadItem, intersections[k].toPointF())) {
                        trimmedThisItem = true;
                        break;
                    }
                }

                if (trimmedThisItem) {
                    trimmedAny = true;
                    break;
                }
            }
        }
    } while (trimmedAny);
}

void TrimTool::updateHoverPreview(const QPointF &pos) {
    if (!m_scene)
        return;

    QGraphicsItem *item = itemAtPos(pos);
    CadItem *cadItem = dynamic_cast<CadItem *>(item);

    if (!cadItem) {
        clearHoverPreview();
        return;
    }

    std::vector<MyVector> intersections = findAllIntersections(cadItem);
    if (intersections.empty()) {
        clearHoverPreview();
        return;
    }

    const MyEntity *entity = cadItem->getEntityData();
    if (!entity)
        return;

    MyVector center;
    double radius = 0.0;
    auto getParamFunc = [&](const MyVector &pt) -> double {
        return getParam(entity, pt);
    };

    std::vector<double> params;
    double clickT = 0.0;
    double targetStartAngle = 0;
    double targetSpanAngle = 360;

    if (entity->type() == EntityType::Circle && !intersections.empty()) {
        const MyCircle *c = static_cast<const MyCircle *>(entity);
        center = c->center;
        radius = c->radius;
        double baseAngle = MyMath::rad2deg(std::atan2(
            intersections[0].y - center.y, intersections[0].x - center.x));
        targetStartAngle = MyMath::normalizeAngle(baseAngle);
        targetSpanAngle = 360.0;

        params.push_back(0.0);
        params.push_back(1.0);
        for (size_t i = 0; i < intersections.size(); ++i) {
            const auto &pt = intersections[i];
            double ang =
                MyMath::rad2deg(std::atan2(pt.y - center.y, pt.x - center.x));
            double t = MyMath::normalizeAngle(ang - targetStartAngle) / 360.0;
            if (t > 0.001 && t < 0.999)
                params.push_back(t);
        }
        clickT =
            MyMath::normalizeAngle(MyMath::rad2deg(std::atan2(pos.y() - center.y,
                                                              pos.x() - center.x)) -
                                   targetStartAngle) /
            360.0;
    } else {
        params.push_back(0.0);
        params.push_back(1.0);
        for (size_t i = 0; i < intersections.size(); ++i) {
            double t = getParamFunc(intersections[i]);
            if (t > 0.001 && t < 0.999)
                params.push_back(t);
        }
        clickT = getParamFunc(MyVector(pos));

        if (entity->type() == EntityType::Arc) {
            const MyArc *a = static_cast<const MyArc *>(entity);
            center = a->center;
            radius = a->radius;
            targetStartAngle = a->startAngle;
            targetSpanAngle = a->spanAngle;
        } else if (entity->type() == EntityType::Circle) {
            const MyCircle *c = static_cast<const MyCircle *>(entity);
            center = c->center;
            radius = c->radius;
            targetStartAngle = 0;
            targetSpanAngle = 360;
        }
    }

    std::sort(params.begin(), params.end());
    params.erase(
        std::unique(params.begin(), params.end(),
                    [](double a, double b) { return std::abs(a - b) < 0.001; }),
        params.end());

    if (params.size() < 3) {
        clearHoverPreview();
        return;
    }

    int idx = -1;
    for (size_t i = 0; i < params.size() - 1; ++i) {
        if (clickT >= params[i] && clickT <= params[i + 1]) {
            idx = i;
            break;
        }
    }

    if (idx == -1) {
        clearHoverPreview();
        return;
    }

    QPainterPath path;
    double t1 = params[idx];
    double t2 = params[idx + 1];

    if (entity->type() == EntityType::Line) {
        const MyLine *line = static_cast<const MyLine *>(entity);
        MyVector p1 = line->start + (line->end - line->start) * t1;
        MyVector p2 = line->start + (line->end - line->start) * t2;
        path.moveTo(p1.toPointF());
        path.lineTo(p2.toPointF());
    } else if (entity->type() == EntityType::Arc ||
               entity->type() == EntityType::Circle) {
        double s = targetStartAngle + targetSpanAngle * t1;
        double sp = targetSpanAngle * (t2 - t1);
        QRectF rect(center.x - radius, center.y - radius, radius * 2, radius * 2);
        path.arcMoveTo(rect, -s);
        path.arcTo(rect, -s, -sp);
    } else if (entity->type() == EntityType::Polyline) {
        const MyPolyline *pline = static_cast<const MyPolyline *>(entity);
        size_t n =
            pline->isClosed ? pline->vertices.size() : pline->vertices.size() - 1;
        double p1 = t1 * n;
        double p2 = t2 * n;

        int idx1 = std::floor(p1 + 0.0001);
        if (idx1 >= static_cast<int>(n))
            idx1 = static_cast<int>(n) - 1;
        double localT1 = p1 - idx1;
        MyVector v1 = pline->vertices[idx1];
        MyVector v2 = pline->vertices[(idx1 + 1) % pline->vertices.size()];
        MyVector startPt = v1 + (v2 - v1) * localT1;
        path.moveTo(startPt.toPointF());

        for (int i = idx1 + 1; i <= std::floor(p2 - 0.0001); ++i) {
            path.lineTo(pline->vertices[i % pline->vertices.size()].toPointF());
        }

        int idx2 = std::floor(p2 - 0.0001);
        if (idx2 >= static_cast<int>(n))
            idx2 = static_cast<int>(n) - 1;
        double localT2 = p2 - idx2;
        MyVector v3 = pline->vertices[idx2];
        MyVector v4 = pline->vertices[(idx2 + 1) % pline->vertices.size()];
        MyVector endPt = v3 + (v4 - v3) * localT2;
        path.lineTo(endPt.toPointF());
    } else if (entity->type() == EntityType::Spline) {
        const MySpline *spline = static_cast<const MySpline *>(entity);
        std::vector<MyVector> pts = MyMath::approximateSpline(*spline);
        if (pts.size() >= 2) {
            int n = (int)pts.size() - 1;
            double p1 = t1 * n;
            double p2 = t2 * n;

            int idx1 = std::floor(p1 + 0.0001);
            if (idx1 >= n) idx1 = n - 1;
            double localT1 = p1 - idx1;
            MyVector startPt = pts[idx1] + (pts[idx1 + 1] - pts[idx1]) * localT1;
            path.moveTo(startPt.toPointF());

            for (int i = idx1 + 1; i <= std::floor(p2 - 0.0001); ++i) {
                path.lineTo(pts[i].toPointF());
            }

            int idx2 = std::floor(p2 - 0.0001);
            if (idx2 >= n) idx2 = n - 1;
            double localT2 = p2 - idx2;
            MyVector endPt = pts[idx2] + (pts[idx2 + 1] - pts[idx2]) * localT2;
            path.lineTo(endPt.toPointF());
        }
    }

    updateGhostPreview(path, cadItem->layerName());
}

void TrimTool::clearHoverPreview() {
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
    }
}

bool TrimTool::processCommand(const QString &command) {
    if (command.isEmpty() && m_currentState == State_SelectingCuttingEdges) {
        if (m_cuttingEdges.empty()) {
            m_selectAllMode = true;
        }
        m_currentState = State_SelectingTrimSegment;
        updatePrompt();
        return true;
    }
    return false;
}

void TrimTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Space) {
        if (m_currentState == State_SelectingCuttingEdges) {
            if (m_cuttingEdges.empty()) {
                m_selectAllMode = true;
            }
            m_currentState = State_SelectingTrimSegment;
            updatePrompt();
        } else {
            if (m_isFenceSelecting) {
                m_isFenceSelecting = false;
                clearHoverPreview();
            } else {
                emit toolFinished();
            }
        }
    } else if (event->key() == Qt::Key_Escape) {
        if (m_isFenceSelecting) {
            m_isFenceSelecting = false;
            clearHoverPreview();
        } else {
            emit toolFinished();
        }
    } else {
        AbstractTool::keyPress(event);
    }
}

std::vector<MyVector> TrimTool::findAllIntersections(CadItem *targetItem) {
    std::vector<MyVector> intersections;
    const MyEntity *targetEntity = targetItem->getEntityData();
    if (!targetEntity)
        return intersections;

    std::vector<CadItem *> edges;
    if (m_selectAllMode) {
        QList<QGraphicsItem *> items = m_scene->items();
        for (int i = 0; i < items.size(); ++i) {
            CadItem *ci = dynamic_cast<CadItem *>(items[i]);
            if (ci && ci != targetItem && ci->scene() != nullptr)
                edges.push_back(ci);
        }
    } else {
        for (size_t i = 0; i < m_cuttingEdges.size(); ++i) {
            CadItem *edge = m_cuttingEdges[i];
            if (edge && edge != targetItem && edge->scene() != nullptr)
                edges.push_back(edge);
        }
    }

    for (size_t i = 0; i < edges.size(); ++i) {
        CadItem *edgeItem = edges[i];
        const MyEntity *edgeEntity = edgeItem->getEntityData();
        if (!edgeEntity)
            continue;

        std::vector<MyVector> pts = targetEntity->intersect(edgeEntity);
        for (size_t j = 0; j < pts.size(); ++j) {
            const auto &pt = pts[j];
            bool exists = false;
            for (size_t k = 0; k < intersections.size(); ++k) {
                if (pt.distanceTo(intersections[k]) < 0.001) {
                    exists = true;
                    break;
                }
            }
            if (!exists)
                intersections.push_back(pt);
        }
    }

    return intersections;
}

bool TrimTool::tryTrim(CadItem *targetItem, const QPointF &clickPos) {
    std::vector<MyVector> intersections = findAllIntersections(targetItem);
    if (intersections.empty())
        return false;

    bool result = executeTrim(targetItem, intersections, clickPos);
    updatePrompt();
    return result;
}

bool TrimTool::executeTrim(CadItem *targetItem,
                           const std::vector<MyVector> &intersections,
                           const QPointF &clickPos) {
    const MyEntity *targetEntity = targetItem->getEntityData();
    if (!targetEntity)
        return false;

    std::vector<double> params;
    MyVector clickVec(clickPos.x(), clickPos.y());
    double clickT = -1.0;
    double targetStartAngle = 0;
    double targetSpanAngle = 360;

    if (targetEntity->type() == EntityType::Circle && !intersections.empty()) {
        const MyCircle *c = static_cast<const MyCircle *>(targetEntity);
        double baseAngle = MyMath::rad2deg(std::atan2(
            intersections[0].y - c->center.y, intersections[0].x - c->center.x));
        targetStartAngle = MyMath::normalizeAngle(baseAngle);
        targetSpanAngle = 360.0;

        params.push_back(0.0);
        params.push_back(1.0);
        for (size_t i = 0; i < intersections.size(); ++i) {
            const auto &pt = intersections[i];
            double ang =
                MyMath::rad2deg(std::atan2(pt.y - c->center.y, pt.x - c->center.x));
            double t = MyMath::normalizeAngle(ang - targetStartAngle) / 360.0;
            if (t > 0.001 && t < 0.999)
                params.push_back(t);
        }
        clickT = MyMath::normalizeAngle(
                     MyMath::rad2deg(std::atan2(clickPos.y() - c->center.y,
                                                clickPos.x() - c->center.x)) -
                     targetStartAngle) /
                 360.0;
    } else {
        params.push_back(0.0);
        params.push_back(1.0);
        auto getParamFunc = [&](const MyVector &pt) -> double {
            return getParam(targetEntity, pt);
        };
        for (size_t i = 0; i < intersections.size(); ++i) {
            double t = getParamFunc(intersections[i]);
            if (t > 0.001 && t < 0.999)
                params.push_back(t);
        }
        clickT = getParamFunc(clickVec);

        if (targetEntity->type() == EntityType::Arc) {
            const MyArc *a = static_cast<const MyArc *>(targetEntity);
            targetStartAngle = a->startAngle;
            targetSpanAngle = a->spanAngle;
        } else if (targetEntity->type() == EntityType::Circle) {
            targetStartAngle = 0;
            targetSpanAngle = 360;
        }
    }

    std::sort(params.begin(), params.end());
    params.erase(
        std::unique(params.begin(), params.end(),
                    [](double a, double b) { return std::abs(a - b) < 0.001; }),
        params.end());

    if (params.size() < 3)
        return false;

    int clickIdx = -1;
    for (size_t i = 0; i < params.size() - 1; ++i) {
        if (clickT >= params[i] && clickT <= params[i + 1]) {
            clickIdx = i;
            break;
        }
    }

    if (clickIdx == -1)
        return false;

    // =========================================================================
    // [핵심 해결 로직] 객체 파편화(Fragmentation) 방지를 위한 인접 구간 병합
    // =========================================================================

    // 1. 클릭된 구간(삭제 대상)을 제외한 나머지 유효한 구간(Spans)을 수집합니다.
    std::vector<std::pair<double, double>> validSpans;
    for (size_t i = 0; i < params.size() - 1; ++i) {
        if (static_cast<int>(i) == clickIdx)
            continue;
        validSpans.push_back({params[i], params[i + 1]});
    }

    // 2. 인접한(연속된) 구간들을 하나로 합칩니다.
    // 이전에는 모든 구간을 개별적인 객체로 나누어 생성했기 때문에 트림 시
    // 선이 여러 조각으로 깨졌고, 이후 교차점 검색 로직이 작동하지 않았습니다.
    std::vector<std::pair<double, double>> mergedSpans;
    if (!validSpans.empty()) {
        mergedSpans.push_back(validSpans[0]);
        for (size_t i = 1; i < validSpans.size(); ++i) {
            // 이전 구간의 끝(second)과 현재 구간의 시작(first)이 맞닿아 있다면 하나로 병합
            if (std::abs(mergedSpans.back().second - validSpans[i].first) < 0.001) {
                mergedSpans.back().second = validSpans[i].second;
            } else {
                mergedSpans.push_back(validSpans[i]);
            }
        }
    }

    auto factory = m_appContext->entityFactory();
    auto curPen = targetItem->pen();

    QUndoStack *undoStack = (m_appContext) ? m_appContext->undoStack() : nullptr;
    if (undoStack) {
        undoStack->beginMacro(tr("Trim"));
        undoStack->push(new RemoveEntityCommand(m_scene, targetItem));
    }

    std::vector<CadItem*> newlyCreatedItems;

    // 병합된(mergedSpans) 온전한 구간 데이터를 기반으로 새 객체를 생성합니다.
    for (size_t i = 0; i < mergedSpans.size(); ++i) {
        double t1 = mergedSpans[i].first;
        double t2 = mergedSpans[i].second;

        QGraphicsItem *newItem = nullptr;
        if (targetEntity->type() == EntityType::Line) {
            const MyLine *line = static_cast<const MyLine *>(targetEntity);
            MyVector p1 = line->start + (line->end - line->start) * t1;
            MyVector p2 = line->start + (line->end - line->start) * t2;
            newItem =
                factory->createLine(QLineF(p1.toPointF(), p2.toPointF()), curPen);
        } else if (targetEntity->type() == EntityType::Arc ||
                   targetEntity->type() == EntityType::Circle) {
            MyVector center;
            double radius;
            if (targetEntity->type() == EntityType::Circle) {
                center = static_cast<const MyCircle *>(targetEntity)->center;
                radius = static_cast<const MyCircle *>(targetEntity)->radius;
            } else {
                center = static_cast<const MyArc *>(targetEntity)->center;
                radius = static_cast<const MyArc *>(targetEntity)->radius;
            }
            double s = targetStartAngle + targetSpanAngle * t1;
            double sp = targetSpanAngle * (t2 - t1);
            newItem = factory->createArc(center.toPointF(), radius, s, sp, curPen);
        } else if (targetEntity->type() == EntityType::Polyline) {
            const MyPolyline *pline = static_cast<const MyPolyline *>(targetEntity);
            size_t n =
                pline->isClosed ? pline->vertices.size() : pline->vertices.size() - 1;
            double p1 = t1 * n;
            double p2 = t2 * n;

            std::vector<MyVector> newVertices;
            int idx1 = std::floor(p1 + 0.0001);
            if (idx1 >= static_cast<int>(n))
                idx1 = static_cast<int>(n) - 1;
            double localT1 = p1 - idx1;
            MyVector v1 = pline->vertices[idx1];
            MyVector v2 = pline->vertices[(idx1 + 1) % pline->vertices.size()];
            newVertices.push_back(v1 + (v2 - v1) * localT1);

            for (int k = idx1 + 1; k <= std::floor(p2 - 0.0001); ++k) {
                newVertices.push_back(pline->vertices[k % pline->vertices.size()]);
            }

            int idx2 = std::floor(p2 - 0.0001);
            if (idx2 >= static_cast<int>(n))
                idx2 = static_cast<int>(n) - 1;
            double localT2 = p2 - idx2;
            MyVector v3 = pline->vertices[idx2];
            MyVector v4 = pline->vertices[(idx2 + 1) % pline->vertices.size()];
            MyVector vend = v3 + (v4 - v3) * localT2;

            if (newVertices.empty() || newVertices.back().distanceTo(vend) > 0.001) {
                newVertices.push_back(vend);
            }

            if (newVertices.size() >= 2) {
                newItem = factory->createPolyline(newVertices, false, curPen);
            }
        } else if (targetEntity->type() == EntityType::Spline) {
            const MySpline *spline = static_cast<const MySpline *>(targetEntity);
            std::vector<MyVector> pts = MyMath::approximateSpline(*spline);
            if (pts.size() >= 2) {
                int n = (int)pts.size() - 1;
                double p1 = t1 * n;
                double p2 = t2 * n;

                std::vector<MyVector> newVertices;
                int idx1 = std::floor(p1 + 0.0001);
                if (idx1 >= n) idx1 = n - 1;
                double localT1 = p1 - idx1;
                newVertices.push_back(pts[idx1] + (pts[idx1+1] - pts[idx1]) * localT1);

                for (int k = idx1 + 1; k <= std::floor(p2 - 0.0001); ++k) {
                    newVertices.push_back(pts[k]);
                }

                int idx2 = std::floor(p2 - 0.0001);
                if (idx2 >= n) idx2 = n - 1;
                double localT2 = p2 - idx2;
                MyVector vend = pts[idx2] + (pts[idx2+1] - pts[idx2]) * localT2;

                if (newVertices.empty() || newVertices.back().distanceTo(vend) > 0.001) {
                    newVertices.push_back(vend);
                }

                if (newVertices.size() >= 2) {
                    newItem = factory->createPolyline(newVertices, false, curPen);
                }
            }
        }

        if (newItem) {
            if (CadItem *ci = dynamic_cast<CadItem *>(newItem)) {
                ci->setLayerName(targetItem->layerName());
                // 트림 후 생성된 새 선분에 ML 태그 복원
                if (LineItem *newLi = dynamic_cast<LineItem *>(ci)) {
                    if (LineItem *origLi = dynamic_cast<LineItem *>(targetItem)) {
                        const QString gid = origLi->data(ML_DATA_GROUP_ID).toString();
                        if (!gid.isEmpty()) {
                            newLi->setData(ML_DATA_GROUP_ID, gid);
                            newLi->setData(ML_DATA_ROLE,     origLi->data(ML_DATA_ROLE));
                            const double sp = origLi->data(ML_DATA_SPACING).toDouble();
                            if (sp > 0.0) newLi->setData(ML_DATA_SPACING, sp);
                        }
                    }
                }
                if (undoStack) {
                    undoStack->push(new AddEntityCommand(m_scene, ci));
                } else {
                    m_scene->addItem(ci);
                    m_scene->reparentItemToLayer(ci, ci->layerName());
                }
                newlyCreatedItems.push_back(ci);
            }
        }
    }

    if (undoStack) {
        undoStack->endMacro();
    }

    auto it = std::find(m_cuttingEdges.begin(), m_cuttingEdges.end(), targetItem);
    if (it != m_cuttingEdges.end()) {
        m_cuttingEdges.erase(it);
        for (size_t i = 0; i < newlyCreatedItems.size(); ++i) {
            m_cuttingEdges.push_back(newlyCreatedItems[i]);
        }
    }

    if (!undoStack) {
        m_scene->removeItem(targetItem);
        delete targetItem;
    }

    return true;
}

double TrimTool::getParam(const MyEntity *entity, const MyVector &pt) {
    if (entity->type() == EntityType::Line) {
        const MyLine *line = static_cast<const MyLine *>(entity);
        MyVector ab = line->end - line->start;
        MyVector ap = pt - line->start;
        double lenSq = MyMath::dotProduct(ab, ab);
        if (lenSq < 0.001)
            return 0.0;
        double t = MyMath::dotProduct(ap, ab) / lenSq;
        return std::max(0.0, std::min(1.0, t));
    } else if (entity->type() == EntityType::Arc ||
               entity->type() == EntityType::Circle) {
        MyVector center;
        double startAngle, spanAngle;
        if (entity->type() == EntityType::Circle) {
            const MyCircle *c = static_cast<const MyCircle *>(entity);
            center = c->center;
            startAngle = 0;
            spanAngle = 360;
        } else {
            const MyArc *a = static_cast<const MyArc *>(entity);
            center = a->center;
            startAngle = a->startAngle;
            spanAngle = a->spanAngle;
        }
        double ang = MyMath::normalizeAngle(
            MyMath::rad2deg(std::atan2(pt.y - center.y, pt.x - center.x)));

        double offset;
        if (spanAngle >= 0) {
            offset = MyMath::normalizeAngle(ang - startAngle);
        } else {
            offset = MyMath::normalizeAngle(startAngle - ang);
        }

        if (std::abs(spanAngle) < 0.001)
            return 0.0;
        return offset / std::abs(spanAngle);
    } else if (entity->type() == EntityType::Polyline) {
        const MyPolyline *pline = static_cast<const MyPolyline *>(entity);
        size_t count =
            pline->isClosed ? pline->vertices.size() : pline->vertices.size() - 1;
        if (count == 0)
            return 0.0;

        for (size_t i = 0; i < count; ++i) {
            MyVector v1 = pline->vertices[i];
            MyVector v2 = pline->vertices[(i + 1) % pline->vertices.size()];
            MyLine seg(v1, v2);
            MyVector nearest = MyMath::getNearestPointOnSegment(pt, seg);
            if (nearest.distanceTo(pt) < 1.0) {
                MyVector ab = seg.end - seg.start;
                MyVector ap = nearest - seg.start;
                double lenSq = MyMath::dotProduct(ab, ab);
                double localT =
                    (lenSq < 0.001) ? 0.0 : MyMath::dotProduct(ap, ab) / lenSq;
                return (double(i) + std::max(0.0, std::min(1.0, localT))) /
                       double(count);
            }
        }
    } else if (entity->type() == EntityType::Spline) {
        const MySpline *spline = static_cast<const MySpline *>(entity);
        std::vector<MyVector> pts = MyMath::approximateSpline(*spline);
        if (pts.size() < 2) return 0.0;

        double minDist = 1e18;
        double bestParam = 0.0;
        int n = (int)pts.size() - 1;

        for (int i = 0; i < n; ++i) {
            MyLine seg(pts[i], pts[i+1]);
            MyVector nearest = MyMath::getNearestPointOnSegment(pt, seg);
            double d = nearest.distanceTo(pt);
            if (d < minDist) {
                minDist = d;
                MyVector ab = seg.end - seg.start;
                MyVector ap = nearest - seg.start;
                double lenSq = MyMath::dotProduct(ab, ab);
                double localT = (lenSq < 1e-9) ? 0.0 : MyMath::dotProduct(ap, ab) / lenSq;
                bestParam = (double(i) + std::max(0.0, std::min(1.0, localT))) / double(n);
            }
        }
        return bestParam;
    }
    return 0.0;
}