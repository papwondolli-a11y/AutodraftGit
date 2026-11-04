#include "fillettool.h"
#include "appcontext.h"
#include "arcitem.h"
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
#include <QApplication>
#include <QKeyEvent>
#include <cmath>

double FilletTool::s_lastRadius = 0.0;

FilletTool::FilletTool(QObject *parent)
    : AbstractTool(parent), m_currentState(State_WaitingForFirst),
    m_firstItem(nullptr), m_radius(s_lastRadius), m_isBoxSelecting(false) {}

FilletTool::~FilletTool() { clearOverlay(); }

void FilletTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene)
        return;
    AbstractTool::activate(scene);
    m_currentState = State_WaitingForFirst;
    m_firstItem = nullptr;
    m_radius = s_lastRadius;
    m_isBoxSelecting = false;
    hideGripsOfSelectedItems();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("FILLET", CommandDock::Command);
    }
    updatePrompt();
}

void FilletTool::deactivate() {
    if (m_firstItem) {
        m_firstItem->setGripsEnabled(true);
        m_firstItem->setSelected(false);
    }
    clearOverlay();
    clearHoverPreview();
    m_firstItem = nullptr;
    s_lastRadius = m_radius;
    AbstractTool::deactivate();
}

void FilletTool::updatePrompt() {
    if (!m_appContext)
        return;
    auto dock = m_appContext->commandDock();
    if (!dock)
        return;

    QString radiusStr = QString::number(m_radius);
    if (m_currentState == State_WaitingForFirst) {
        dock->showInteractivePrompt(
            tr(" 첫번째 객체를 선택 하세요 (Radius=%1) [Radius]:").arg(radiusStr), {"R"});
    } else {
        dock->showInteractivePrompt(tr(" 두번째 객체를 선택 하세요 (Shift: 원본 유지) :"), {});
    }
}

void FilletTool::mousePress(QPointF pos, Qt::MouseButton button) {
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

                bool trimLines = !(QApplication::keyboardModifiers() & Qt::ShiftModifier);
                tryFillet(m_firstItem, cadItem, m_firstPickPos, pos, trimLines);

                m_currentState = State_WaitingForFirst;
                m_firstItem = nullptr;
                updatePrompt();
            }
        }
    } else {
        m_isBoxSelecting = true;
        m_firstCorner = pos;
        clearOverlay();
    }
}

void FilletTool::mouseMove(QPointF pos) {
    if (m_isBoxSelecting) {
        handleSelectionMove(pos);
    } else if (m_currentState == State_WaitingForSecond) {
        updateHoverPreview(pos);
    }
}

void FilletTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
    Q_UNUSED(pos);
    Q_UNUSED(button);
}

void FilletTool::handleSelectionMove(QPointF pos) {
    if (!m_scene) return;
    m_scene->setSelectionRect(m_firstCorner, pos, MyGraphicsScene::Mode_Crossing);
}

void FilletTool::processBoxSelection(QPointF p2) {
    m_isBoxSelecting = false;
    clearOverlay();

    QRectF rect = QRectF(m_firstCorner, p2).normalized();
    QList<QGraphicsItem *> items = m_scene->items(rect, Qt::IntersectsItemShape);

    std::vector<CadItem *> candidates;
    for (int i = 0; i < items.size(); ++i) {
        QGraphicsItem *item = items[i];
        if (CadItem *ci = dynamic_cast<CadItem *>(item)) {
            candidates.push_back(ci);
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
            bool trimLines = !(QApplication::keyboardModifiers() & Qt::ShiftModifier);
            tryFillet(item1, item2, item1->sceneBoundingRect().center(),
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

void FilletTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return ||
        event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) {
        emit toolFinished();
    } else {
        AbstractTool::keyPress(event);
    }
}

bool FilletTool::processCommand(const QString &command) {
    QString cmd = command.toUpper().trimmed();
    if (cmd == "R" || cmd == "RADIUS") {
        m_appContext->commandDock()->showInteractivePrompt(
            tr("R값을 입력 하세요 <%1>:").arg(m_radius), {});
        return true;
    }

    bool ok;
    double r = cmd.toDouble(&ok);
    if (ok) {
        m_radius = std::abs(r);
        s_lastRadius = m_radius;
        updatePrompt();
        return true;
    }
    return false;
}

void FilletTool::clearOverlay() {
    if (m_scene) m_scene->clearOverlay();
}

void FilletTool::tryFillet(CadItem *item1, CadItem *item2, const QPointF &p1,
                           const QPointF &p2, bool trimLines) {
    if (!item1 || !item2 || item1 == item2) return;
    executeFillet(item1, item2, p1, p2, trimLines);
}

// ---------------------------------------------------------------------------------
// [핵심] 선과 원(호) 사이의 기하학적 접점(Fillet)을 도출하는 헬퍼 함수
// ---------------------------------------------------------------------------------
LineCircleFilletResult FilletTool::calcLineCircleFillet(const MyLine *line, const MyVector &cCenter,
                                                        double cRadius, double fRadius,
                                                        const QPointF &pPickLine, const QPointF &pPickCirc) {
    LineCircleFilletResult res;

    MyVector vDir = (line->end - line->start);
    if (vDir.magnitude() < 1e-6) return res;
    vDir = vDir.normalized();
    MyVector vNorm(-vDir.y, vDir.x);

    // 클릭 위치를 기반으로 라인의 오프셋 방향 결정
    MyVector toClick = MyVector(pPickLine) - line->start;
    if (MyMath::dotProduct(toClick, vNorm) < 0) {
        vNorm = MyVector(vDir.y, -vDir.x);
    }

    // 매우 긴 오프셋 가상선 생성 (무한 직선 취급)
    MyVector offsetStart = line->start + vNorm * fRadius - vDir * 100000.0;
    MyVector offsetEnd = line->end + vNorm * fRadius + vDir * 100000.0;
    MyLine offsetLine(offsetStart, offsetEnd);

    // 원의 오프셋 (외접 Fillet 가정)
    MyCircle offsetCirc(cCenter, cRadius + fRadius);
    std::vector<MyVector> centers = MyMath::intersectLineCircle(offsetLine, offsetCirc);

    // 외접 교차점이 없다면 내접(Internal fillet) 시도
    if (centers.empty() && cRadius > fRadius) {
        offsetCirc = MyCircle(cCenter, std::abs(cRadius - fRadius));
        centers = MyMath::intersectLineCircle(offsetLine, offsetCirc);
    }

    if (centers.empty()) return res; // 조건 만족 불가능 시 종료

    // 클릭 지점들과 가장 가까운 중심점(Fillet Arc의 중심) 채택
    MyVector bestCenter = centers[0];
    if (centers.size() > 1) {
        double d1 = centers[0].distanceTo(MyVector(pPickLine)) + centers[0].distanceTo(MyVector(pPickCirc));
        double d2 = centers[1].distanceTo(MyVector(pPickLine)) + centers[1].distanceTo(MyVector(pPickCirc));
        if (d2 < d1) bestCenter = centers[1];
    }

    res.center = bestCenter;
    // 라인 쪽 접점
    res.tLine = MyMath::getPerpendicularPointOnLine(bestCenter, *line);

    // 원 쪽 접점
    MyVector cToF = bestCenter - cCenter;
    double distCtoF = cToF.magnitude();
    if (distCtoF < 1e-6) return res;
    res.tCirc = cCenter + cToF.normalized() * cRadius;

    // 그릴 호(Arc)의 각도 계산
    res.a1 = MyMath::rad2deg(std::atan2(res.tLine.y - bestCenter.y, res.tLine.x - bestCenter.x));
    double a2 = MyMath::rad2deg(std::atan2(res.tCirc.y - bestCenter.y, res.tCirc.x - bestCenter.x));

    // 최단 거리(180도 이하) 방향으로 호를 그림
    res.span = MyMath::normalizeAngle(a2 - res.a1);
    if (res.span > 180) res.span -= 360;
    if (res.span < -180) res.span += 360;

    res.valid = true;
    return res;
}

CircleCircleFilletResult FilletTool::calcCircleCircleFillet(const MyVector &c1Center, double c1Radius,
                                                            const MyVector &c2Center, double c2Radius,
                                                            double fRadius, const QPointF &p1, const QPointF &p2) {
    CircleCircleFilletResult res;
    if (fRadius < 1e-6) return res;

    std::vector<MyVector> candidates;
    double r1_plus = c1Radius + fRadius;
    double r1_minus = std::abs(c1Radius - fRadius);
    double r2_plus = c2Radius + fRadius;
    double r2_minus = std::abs(c2Radius - fRadius);

    auto addInters = [&](double r1, double r2) {
        auto inters = MyMath::intersectCircleCircle(MyCircle(c1Center, r1), MyCircle(c2Center, r2));
        for (const auto &p : inters) candidates.push_back(p);
    };

    addInters(r1_plus, r2_plus);
    addInters(r1_plus, r2_minus);
    addInters(r1_minus, r2_plus);
    if (c1Radius > fRadius && c2Radius > fRadius) addInters(r1_minus, r2_minus);

    if (candidates.empty()) return res;

    MyVector bestCenter = candidates[0];
    auto pick1 = MyVector(p1);
    auto pick2 = MyVector(p2);
    double minDist = bestCenter.distanceTo(pick1) + bestCenter.distanceTo(pick2);

    for (size_t i = 1; i < candidates.size(); ++i) {
        double d = candidates[i].distanceTo(pick1) + candidates[i].distanceTo(pick2);
        if (d < minDist) {
            minDist = d;
            bestCenter = candidates[i];
        }
    }

    res.center = bestCenter;
    // 접점 T1
    MyVector v1 = bestCenter - c1Center;
    if (v1.magnitude() < 1e-6) return res;
    res.t1 = c1Center + v1.normalized() * c1Radius;

    // 접점 T2
    MyVector v2 = bestCenter - c2Center;
    if (v2.magnitude() < 1e-6) return res;
    res.t2 = c2Center + v2.normalized() * c2Radius;

    res.a1 = MyMath::rad2deg(std::atan2(res.t1.y - bestCenter.y, res.t1.x - bestCenter.x));
    double a2 = MyMath::rad2deg(std::atan2(res.t2.y - bestCenter.y, res.t2.x - bestCenter.x));

    res.span = MyMath::normalizeAngle(a2 - res.a1);
    if (res.span > 180) res.span -= 360;
    if (res.span < -180) res.span += 360;

    res.valid = true;
    return res;
}

void FilletTool::executeFillet(CadItem *item1, CadItem *item2,
                               const QPointF &p1, const QPointF &p2, bool trimLines) {
    const MyEntity *e1 = item1->getEntityData();
    const MyEntity *e2 = item2->getEntityData();

    bool isL1 = (e1->type() == EntityType::Line);
    bool isL2 = (e2->type() == EntityType::Line);
    bool isC1 = (e1->type() == EntityType::Circle || e1->type() == EntityType::Arc);
    bool isC2 = (e2->type() == EntityType::Circle || e2->type() == EntityType::Arc);

    // 1. 기존 Line vs Line
    if (isL1 && isL2) {
        const MyLine *l1 = static_cast<const MyLine *>(e1);
        const MyLine *l2 = static_cast<const MyLine *>(e2);

        std::vector<MyVector> inters = MyMath::intersectLineLineInfinite(*l1, *l2);
        if (inters.empty()) return;

        MyVector P = inters[0];
        MyVector v1 = l1->end - l1->start;
        MyVector v2 = l2->end - l2->start;
        if (v1.magnitude() < 1e-6 || v2.magnitude() < 1e-6) return;
        v1 = v1.normalized();
        v2 = v2.normalized();

        MyVector clickDir1 = MyVector(p1) - P;
        MyVector d1 = (MyMath::dotProduct(clickDir1, v1) >= 0) ? v1 : MyVector(-v1.x, -v1.y);

        MyVector clickDir2 = MyVector(p2) - P;
        MyVector d2 = (MyMath::dotProduct(clickDir2, v2) >= 0) ? v2 : MyVector(-v2.x, -v2.y);

        if (m_radius < 0.001) {
            if (trimLines) {
                QUndoStack *stack = m_appContext->undoStack();
                if (stack) stack->beginMacro("Fillet (R=0)");

                double dotStart1 = MyMath::dotProduct(l1->start - P, d1);
                double dotEnd1 = MyMath::dotProduct(l1->end - P, d1);
                item1->updateEndpoint(P.toPointF(), dotStart1 < dotEnd1);

                double dotStart2 = MyMath::dotProduct(l2->start - P, d2);
                double dotEnd2 = MyMath::dotProduct(l2->end - P, d2);
                item2->updateEndpoint(P.toPointF(), dotStart2 < dotEnd2);

                if (stack) stack->endMacro();
            }
        } else {
            double dot = MyMath::dotProduct(d1, d2);
            if (std::abs(dot) > 0.999) return;

            double angle = std::acos(dot);
            double dist = m_radius / std::tan(angle / 2.0);

            MyVector T1 = P + d1 * dist;
            MyVector T2 = P + d2 * dist;

            MyVector bisect = (d1 + d2).normalized();
            double distToCenter = m_radius / std::sin(angle / 2.0);
            MyVector C = P + bisect * distToCenter;

            double a1 = MyMath::rad2deg(std::atan2(T1.y - C.y, T1.x - C.x));
            double a2 = MyMath::rad2deg(std::atan2(T2.y - C.y, T2.x - C.x));

            double span = MyMath::normalizeAngle(a2 - a1);
            if (span > 180) span -= 360;
            if (span < -180) span += 360;

            QUndoStack *stack = m_appContext->undoStack();
            if (stack) stack->beginMacro("Fillet");

            if (trimLines) {
                double dotStart1 = MyMath::dotProduct(l1->start - P, d1);
                double dotEnd1 = MyMath::dotProduct(l1->end - P, d1);
                item1->updateEndpoint(T1.toPointF(), dotStart1 < dotEnd1);

                double dotStart2 = MyMath::dotProduct(l2->start - P, d2);
                double dotEnd2 = MyMath::dotProduct(l2->end - P, d2);
                item2->updateEndpoint(T2.toPointF(), dotStart2 < dotEnd2);
            }

            auto arc = m_appContext->entityFactory()->createArc(
                C.toPointF(), m_radius, a1, span, item1->pen());
            if (arc) {
                if (CadItem *ci = dynamic_cast<CadItem *>(arc)) {
                    ci->setLayerName(item1->layerName());
                    QGraphicsItem *layerFolder = m_scene->getLayerRoot(item1->layerName());
                    ci->setParentItem(layerFolder);

                    if (ci->scene() == m_scene) {
                        m_scene->removeItem(ci);
                    }

                    if (stack) stack->push(new AddEntityCommand(m_scene, ci));
                    else m_scene->addItem(ci);
                }
            }
            if (stack) stack->endMacro();
        }
    }
    // 2. 추가된 Line vs Circle/Arc 로직
    else if ((isL1 && isC2) || (isC1 && isL2)) {
        CadItem *itemLine = isL1 ? item1 : item2;
        CadItem *itemCirc = isL1 ? item2 : item1;
        QPointF pickLine = isL1 ? p1 : p2;
        QPointF pickCirc = isL1 ? p2 : p1;

        const MyLine *line = static_cast<const MyLine *>(itemLine->getEntityData());
        const MyEntity *circEnt = itemCirc->getEntityData();

        MyVector cCenter;
        double cRadius;
        if (circEnt->type() == EntityType::Circle) {
            cCenter = static_cast<const MyCircle *>(circEnt)->center;
            cRadius = static_cast<const MyCircle *>(circEnt)->radius;
        } else {
            cCenter = static_cast<const MyArc *>(circEnt)->center;
            cRadius = static_cast<const MyArc *>(circEnt)->radius;
        }

        if (m_radius > 0.001) {
            LineCircleFilletResult res = calcLineCircleFillet(line, cCenter, cRadius, m_radius, pickLine, pickCirc);
            if (!res.valid) return;

            QUndoStack *stack = m_appContext->undoStack();
            if (stack) stack->beginMacro("Fillet Line-Circle/Arc");

            if (trimLines) {
                // 선 자르기
                MyVector vDir = line->end - line->start;
                double dotStart = MyMath::dotProduct(line->start - res.tLine, vDir);
                double dotEnd = MyMath::dotProduct(line->end - res.tLine, vDir);
                itemLine->updateEndpoint(res.tLine.toPointF(), dotStart < dotEnd);

                // 원/호 자르기 (객체 내부의 updateEndpoint 의존)
                itemCirc->updateEndpoint(res.tCirc.toPointF(), true);
            }

            auto arc = m_appContext->entityFactory()->createArc(
                res.center.toPointF(), m_radius, res.a1, res.span, itemLine->pen());
            if (arc) {
                if (CadItem *ci = dynamic_cast<CadItem *>(arc)) {
                    ci->setLayerName(itemLine->layerName());
                    QGraphicsItem *layerFolder = m_scene->getLayerRoot(itemLine->layerName());
                    ci->setParentItem(layerFolder);
                    if (ci->scene() == m_scene) {
                        m_scene->removeItem(ci);
                    }
                    if (stack) stack->push(new AddEntityCommand(m_scene, ci));
                    else m_scene->addItem(ci);
                }
            }

            if (stack) stack->endMacro();
        }
    }
    // 3. 추가된 Circle/Arc vs Circle/Arc 로직
    else if (isC1 && isC2) {
        const MyEntity *ent1 = item1->getEntityData();
        const MyEntity *ent2 = item2->getEntityData();
        MyVector c1, c2;
        double r1, r2;

        if (ent1->type() == EntityType::Circle) {
            c1 = static_cast<const MyCircle *>(ent1)->center;
            r1 = static_cast<const MyCircle *>(ent1)->radius;
        } else {
            c1 = static_cast<const MyArc *>(ent1)->center;
            r1 = static_cast<const MyArc *>(ent1)->radius;
        }

        if (ent2->type() == EntityType::Circle) {
            c2 = static_cast<const MyCircle *>(ent2)->center;
            r2 = static_cast<const MyCircle *>(ent2)->radius;
        } else {
            c2 = static_cast<const MyArc *>(ent2)->center;
            r2 = static_cast<const MyArc *>(ent2)->radius;
        }

        if (m_radius > 0.001) {
            CircleCircleFilletResult res = calcCircleCircleFillet(c1, r1, c2, r2, m_radius, p1, p2);
            if (!res.valid) return;

            QUndoStack *stack = m_appContext->undoStack();
            if (stack) stack->beginMacro("Fillet Circle-Circle");

            if (trimLines) {
                item1->updateEndpoint(res.t1.toPointF(), true);
                item2->updateEndpoint(res.t2.toPointF(), true);
            }

            auto arcEntity = m_appContext->entityFactory()->createArc(
                res.center.toPointF(), m_radius, res.a1, res.span, item1->pen());
            if (arcEntity) {
                if (CadItem *ci = dynamic_cast<CadItem *>(arcEntity)) {
                    ci->setLayerName(item1->layerName());
                    QGraphicsItem *layerFolder = m_scene->getLayerRoot(item1->layerName());
                    ci->setParentItem(layerFolder);
                    if (ci->scene() == m_scene) m_scene->removeItem(ci);

                    if (stack) stack->push(new AddEntityCommand(m_scene, ci));
                    else m_scene->addItem(ci);
                }
            }
            if (stack) stack->endMacro();
        }
    }

    // 상태 복구
    if (item1) {
        item1->setGripsEnabled(true);
        item1->setSelected(false);
    }
    if (item2) {
        item2->setGripsEnabled(true);
        item2->setSelected(false);
    }
    clearHoverPreview();
}

void FilletTool::updateHoverPreview(const QPointF &pos) {
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

    bool isL1 = (e1->type() == EntityType::Line);
    bool isL2 = (e2->type() == EntityType::Line);
    bool isC1 = (e1->type() == EntityType::Circle || e1->type() == EntityType::Arc);
    bool isC2 = (e2->type() == EntityType::Circle || e2->type() == EntityType::Arc);

    if (m_radius <= 0.001) return; // R=0 은 미리보기 불필요

    // 1. Line vs Line Preview
    if (isL1 && isL2) {
        const MyLine *l1 = static_cast<const MyLine *>(e1);
        const MyLine *l2 = static_cast<const MyLine *>(e2);

        std::vector<MyVector> inters = MyMath::intersectLineLineInfinite(*l1, *l2);
        if (inters.empty()) return;

        MyVector P = inters[0];

        MyVector v1 = l1->end - l1->start;
        MyVector v2 = l2->end - l2->start;
        if (v1.magnitude() < 1e-6 || v2.magnitude() < 1e-6) return;
        v1 = v1.normalized();
        v2 = v2.normalized();

        MyVector clickDir1 = MyVector(m_firstPickPos) - P;
        MyVector d1 = (MyMath::dotProduct(clickDir1, v1) >= 0) ? v1 : MyVector(-v1.x, -v1.y);

        MyVector clickDir2 = MyVector(pos) - P;
        MyVector d2 = (MyMath::dotProduct(clickDir2, v2) >= 0) ? v2 : MyVector(-v2.x, -v2.y);

        double dot = MyMath::dotProduct(d1, d2);
        if (dot > 1.0) dot = 1.0;
        if (dot < -1.0) dot = -1.0;

        if (std::abs(dot) > 0.9999) return;

        double angle = std::acos(dot);
        double dist = m_radius / std::tan(angle / 2.0);

        MyVector T1 = P + d1 * dist;
        MyVector T2 = P + d2 * dist;

        MyVector bisect = (d1 + d2).normalized();
        double distToCenter = m_radius / std::sin(angle / 2.0);
        MyVector C = P + bisect * distToCenter;

        double a1 = MyMath::rad2deg(std::atan2(T1.y - C.y, T1.x - C.x));
        double a2 = MyMath::rad2deg(std::atan2(T2.y - C.y, T2.x - C.x));
        double span = MyMath::normalizeAngle(a2 - a1);
        if (span > 180) span -= 360;
        if (span < -180) span += 360;

        QPainterPath path = ArcItem::createQtPath(MyArc(C.toPointF(), m_radius, a1, span));
        m_scene->setGhostPath(path, true);
    }
    // 2. Line vs Circle/Arc Preview
    else if ((isL1 && isC2) || (isC1 && isL2)) {
        CadItem *itemLine = isL1 ? m_firstItem : cadItem;
        CadItem *itemCirc = isL1 ? cadItem : m_firstItem;
        QPointF pickLine = isL1 ? m_firstPickPos : pos;
        QPointF pickCirc = isL1 ? pos : m_firstPickPos;

        const MyLine *line = static_cast<const MyLine *>(itemLine->getEntityData());
        const MyEntity *circEnt = itemCirc->getEntityData();

        MyVector cCenter;
        double cRadius;
        if (circEnt->type() == EntityType::Circle) {
            cCenter = static_cast<const MyCircle *>(circEnt)->center;
            cRadius = static_cast<const MyCircle *>(circEnt)->radius;
        } else {
            cCenter = static_cast<const MyArc *>(circEnt)->center;
            cRadius = static_cast<const MyArc *>(circEnt)->radius;
        }

        LineCircleFilletResult res = calcLineCircleFillet(line, cCenter, cRadius, m_radius, pickLine, pickCirc);
        if (res.valid) {
            QPainterPath path = ArcItem::createQtPath(MyArc(res.center.toPointF(), m_radius, res.a1, res.span));
            m_scene->setGhostPath(path, true);
        } else {
            clearHoverPreview();
        }
    }
    // 3. Circle vs Circle Preview
    else if (isC1 && isC2) {
        const MyEntity *ent1 = m_firstItem->getEntityData();
        const MyEntity *ent2 = cadItem->getEntityData();
        MyVector c1, c2;
        double r1, r2;

        if (ent1->type() == EntityType::Circle) {
            c1 = static_cast<const MyCircle *>(ent1)->center;
            r1 = static_cast<const MyCircle *>(ent1)->radius;
        } else {
            c1 = static_cast<const MyArc *>(ent1)->center;
            r1 = static_cast<const MyArc *>(ent1)->radius;
        }

        if (ent2->type() == EntityType::Circle) {
            c2 = static_cast<const MyCircle *>(ent2)->center;
            r2 = static_cast<const MyCircle *>(ent2)->radius;
        } else {
            c2 = static_cast<const MyArc *>(ent2)->center;
            r2 = static_cast<const MyArc *>(ent2)->radius;
        }

        if (m_radius > 0.001) {
            CircleCircleFilletResult res = calcCircleCircleFillet(c1, r1, c2, r2, m_radius, m_firstPickPos, pos);
            if (res.valid) {
                QPainterPath path = ArcItem::createQtPath(MyArc(res.center.toPointF(), m_radius, res.a1, res.span));
                m_scene->setGhostPath(path, true);
            } else {
                clearHoverPreview();
            }
        }
    }
}

void FilletTool::clearHoverPreview() {
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
    }
}

