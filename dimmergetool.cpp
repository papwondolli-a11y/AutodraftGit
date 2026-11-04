#include "dimmergetool.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "lineardimensionitem.h"
#include "modifyentitycommand.h"
#include "mydimension.h"
#include "mygraphicsscene.h"
#include "mymath.h"
#include <QPointer>
#include <QUndoStack>
#include <QtMath>
#include <cmath>

DimMergeTool::DimMergeTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitFirst), m_first(nullptr) {}

DimMergeTool::~DimMergeTool() { resetTool(); }

void DimMergeTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene) return;
    AbstractTool::activate(scene);

    resetTool();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->showInteractivePrompt(tr(" 병합할 첫번째 치수선을 선택하세요 : "), {});
    }
}

void DimMergeTool::deactivate() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(false);
    }
    resetTool();
    AbstractTool::deactivate();
}

void DimMergeTool::resetTool() {
    m_state = WaitFirst;
    m_first = nullptr;
    m_totalSel.clear();
    if (m_scene) m_scene->clearOverlay();
}

bool DimMergeTool::dimDirection(const MyDimension *d, MyVector &dir) {
    double deg;
    if (d->useFixedAngle) {
        deg = d->fixedAngle;
    } else {
        MyVector v = d->end - d->start;
        if (v.magnitude() < 1e-6) return false;
        deg = qRadiansToDegrees(qAtan2(v.y, v.x));
    }
    // 수평/수직에서 미세하게 틀어진 치수는 축 방향으로 보정 (합산값이 .99로 줄어드는 것 방지)
    const double tol = 2.0;
    for (int k = -180; k <= 180; k += 90) {
        if (qAbs(deg - k) <= tol) { deg = double(k); break; }
    }
    double rad = qDegreesToRadians(deg);
    dir = MyVector(qCos(rad), qSin(rad));
    return true;
}

bool DimMergeTool::processCommand(const QString &command) {
    QString cmd = command.trimmed().toLower();
    IAppContext *ctx = getContext();

    if (m_state == WaitOption) {
        if (cmd == "n") {
            createTotalDimension();
            return true;
        }
        if (cmd.isEmpty() || cmd == "m") {
            mergeSelected();
            return true;
        }
    }
    return false;
}

void DimMergeTool::mergeSelected() {
    IAppContext *ctx = getContext();
    if (m_totalSel.size() != 2) {
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->logMessage(tr("병합은 치수선 2개를 선택했을 때만 가능합니다. (N: 토탈치수)"), CommandDock::Info);
        }
        return;
    }
    mergeDimensions(m_totalSel.at(0), m_totalSel.at(1));
}

void DimMergeTool::hideCoincidentExtLines(IAppContext *ctx, const QList<LinearDimensionItem *> &items,
                                          const MyVector &p1, const MyVector &p2) {
    for (int i = 0; i < items.size(); ++i) {
        LinearDimensionItem *li = items.at(i);
        const MyDimension *d = dynamic_cast<const MyDimension *>(li->getEntityData());
        if (!d) continue;
        bool hide1 = d->start.distanceTo(p1) < 1e-3 || d->start.distanceTo(p2) < 1e-3;
        bool hide2 = d->end.distanceTo(p1) < 1e-3 || d->end.distanceTo(p2) < 1e-3;
        if (!hide1 && !hide2) continue;

        QMap<QString, QVariant> oldProps = li->getProperties();
        QMap<QString, QVariant> newProps = oldProps;
        if (hide1) newProps["HideExt1"] = true;
        if (hide2) newProps["HideExt2"] = true;
        if (ctx && ctx->undoStack()) {
            ctx->undoStack()->push(new ModifyEntityCommand(li, oldProps, newProps));
        } else {
            QList<QString> keys = newProps.keys();
            for (int k = 0; k < keys.size(); ++k) {
                li->setProperty(keys.at(k), newProps.value(keys.at(k)));
            }
        }
    }
}

void DimMergeTool::createTotalDimension() {
    IAppContext *ctx = getContext();
    if (m_totalSel.size() < 2) {
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->logMessage(tr("치수선을 2개 이상 선택해야 합니다."), CommandDock::Info);
        }
        finishTool();
        return;
    }

    const MyDimension *d0 = dynamic_cast<const MyDimension *>(m_totalSel.first()->getEntityData());
    MyVector dir;
    if (!d0 || !dimDirection(d0, dir)) { finishTool(); return; }
    MyVector normal(-dir.y, dir.x);

    // 선택된 치수의 정의점 중 가장 바깥쪽 두 점, 치수선은 첫 치수의 P1->P3 방향으로 가장 바깥쪽
    MyVector pMin, pMax;
    double tMin = 0.0, tMax = 0.0;
    double hFirst = MyMath::dotProduct(d0->dimLine - d0->start, normal);
    double sign = (hFirst >= 0) ? 1.0 : -1.0;
    bool init = false;
    for (int i = 0; i < m_totalSel.size(); ++i) {
        const MyDimension *d = dynamic_cast<const MyDimension *>(m_totalSel.at(i)->getEntityData());
        MyVector di;
        if (!d || !dimDirection(d, di) ||
            std::abs(MyMath::dotProduct(dir, MyVector(-di.y, di.x))) > 1e-3) {
            if (ctx && ctx->commandDock()) {
                ctx->commandDock()->logMessage(tr("방향이 다른 치수선이 있어 토탈치수를 만들 수 없습니다."), CommandDock::Info);
            }
            finishTool();
            return;
        }
        MyVector pts[2] = {d->start, d->end};
        for (int k = 0; k < 2; ++k) {
            double t = MyMath::dotProduct(pts[k], dir);
            if (!init || t < tMin) { tMin = t; pMin = pts[k]; }
            if (!init || t > tMax) { tMax = t; pMax = pts[k]; }
            init = true;
        }
    }
    // 치수선 높이는 법선축 절대좌표로 비교
    double base = MyMath::dotProduct(pMin, normal);
    double outerAbs = MyMath::dotProduct(d0->dimLine, normal);
    for (int i = 0; i < m_totalSel.size(); ++i) {
        const MyDimension *d = dynamic_cast<const MyDimension *>(m_totalSel.at(i)->getEntityData());
        double a = MyMath::dotProduct(d->dimLine, normal);
        if ((a - outerAbs) * sign > 0) outerAbs = a;
    }

    double spacing = m_scene->dimensionStyle().baselineSpacing * m_scene->drawingScale();
    double totalAbs = outerAbs + sign * spacing;

    MyDimension data = *d0;
    data.text = "";
    data.extChainH1 = 0.0;
    data.extChainH2 = 0.0;
    data.start = pMin;
    data.end = pMax;
    data.dimLine = pMin + normal * (totalAbs - base);
    data.useFixedAngle = true;
    data.fixedAngle = qRadiansToDegrees(qAtan2(dir.y, dir.x));
    data.hideExt1 = false;
    data.hideExt2 = false;
    data.useCustomTextPos = false;
    data.flipArrow1 = false;
    data.flipArrow2 = false;

    LinearDimensionItem *first = m_totalSel.first();
    LinearDimensionItem *item = new LinearDimensionItem(data);
    item->setLayerName(first->layerName());
    item->setPen(first->pen());
    item->setGripsVisible(false);

    // 토탈 치수보조선과 겹치는 기존 치수보조선은 숨김 (start=ext1, end=ext2)
    if (ctx && ctx->undoStack()) ctx->undoStack()->beginMacro(tr("토탈치수"));
    hideCoincidentExtLines(ctx, m_totalSel, pMin, pMax);

    if (ctx && ctx->undoStack()) {
        ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
        ctx->undoStack()->endMacro();
    } else {
        item->setParentItem(m_scene->getLayerRoot(first->layerName()));
    }
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->logMessage(tr("토탈 치수선이 생성되었습니다."), CommandDock::Info);
    }
    finishTool();
}

void DimMergeTool::finishTool() {
    resetTool();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setPrompt(" Command : ");
    }
    emit toolFinished();
}

void DimMergeTool::onCancel() { finishTool(); }

LinearDimensionItem *DimMergeTool::pickLinearDim(const QPointF &pos) {
    return dynamic_cast<LinearDimensionItem *>(itemAtPos(pos));
}

void DimMergeTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;
    if (button == Qt::RightButton) {
        if (m_state == WaitOption) mergeSelected();
        else finishTool();
        return;
    }
    if (button != Qt::LeftButton) return;

    IAppContext *ctx = getContext();
    LinearDimensionItem *dim = pickLinearDim(pos);
    if (!dim) return;

    if (m_state == WaitFirst) {
        m_first = dim;
        m_totalSel.clear();
        m_totalSel.append(dim);
        m_state = WaitSecond;
        m_scene->setGhostPath(dim->geometryPath(), true, Qt::cyan, 1.0);
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(tr(" 병합할 두번째 치수선을 선택하세요 : "), {});
        }
        return;
    }

    if (m_totalSel.contains(dim)) return;
    m_totalSel.append(dim);
    QPainterPath sel;
    for (int i = 0; i < m_totalSel.size(); ++i) {
        sel.addPath(m_totalSel.at(i)->geometryPath());
    }
    m_scene->setGhostPath(sel, true, Qt::cyan, 1.0);

    if (m_state == WaitSecond) {
        m_state = WaitOption;
    }
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 옵션 [N:토탈치수] <엔터:병합> (치수선 추가 선택 가능) : "), {"n"});
    }
}

void DimMergeTool::mouseMove(QPointF pos) {
    Q_UNUSED(pos);
}

void DimMergeTool::mergeDimensions(LinearDimensionItem *a, LinearDimensionItem *b) {
    IAppContext *ctx = getContext();
    const MyDimension *da = dynamic_cast<const MyDimension *>(a->getEntityData());
    const MyDimension *db = dynamic_cast<const MyDimension *>(b->getEntityData());
    if (!da || !db) return;

    MyVector dir, dirB;
    if (!dimDirection(da, dir) || !dimDirection(db, dirB)) return;
    MyVector normal(-dir.y, dir.x);

    // 두 치수의 방향이 같아야 병합 가능
    if (std::abs(MyMath::dotProduct(dir, MyVector(-dirB.y, dirB.x))) > 1e-3) {
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->logMessage(tr("두 치수선의 방향이 달라 병합할 수 없습니다."), CommandDock::Info);
        }
        return;
    }

    // 네 정의점을 치수 방향으로 투영해 가장 바깥쪽 두 점을 새 치수의 양 끝으로 사용
    MyVector pts[4] = {da->start, da->end, db->start, db->end};
    int minIdx = 0, maxIdx = 0;
    for (int i = 1; i < 4; ++i) {
        double t = MyMath::dotProduct(pts[i], dir);
        if (t < MyMath::dotProduct(pts[minIdx], dir)) minIdx = i;
        if (t > MyMath::dotProduct(pts[maxIdx], dir)) maxIdx = i;
    }

    // 치수선 높이는 먼저 선택한 치수를 따른다
    double h = MyMath::dotProduct(da->dimLine - da->start, normal);

    MyDimension data = *da;
    data.text = "";
    data.start = pts[minIdx];
    data.end = pts[maxIdx];
    data.dimLine = data.start + normal * h;
    data.useFixedAngle = true;
    data.fixedAngle = qRadiansToDegrees(qAtan2(dir.y, dir.x));
    data.hideExt1 = false;
    data.hideExt2 = false;
    data.extChainH1 = 0.0;
    data.extChainH2 = 0.0;
    data.useCustomTextPos = false;
    data.flipArrow1 = false;
    data.flipArrow2 = false;

    LinearDimensionItem *item = new LinearDimensionItem(data);
    item->setLayerName(a->layerName());
    item->setPen(a->pen());
    item->setGripsVisible(false);

    // 병합 구간 사이에 끼어 있는 같은 방향·같은 치수선 높이의 치수도 함께 삭제
    QList<LinearDimensionItem *> removeList;
    removeList.append(a);
    removeList.append(b);
    double tMin = MyMath::dotProduct(data.start, dir);
    double tMax = MyMath::dotProduct(data.end, dir);
    double base = MyMath::dotProduct(data.start, normal);
    QList<QGraphicsItem *> all = m_scene->items();
    for (int i = 0; i < all.size(); ++i) {
        LinearDimensionItem *li = dynamic_cast<LinearDimensionItem *>(all.at(i));
        if (!li || li == a || li == b) continue;
        const MyDimension *dm = dynamic_cast<const MyDimension *>(li->getEntityData());
        if (!dm) continue;

        MyVector dv;
        if (!dimDirection(dm, dv)) continue;
        if (std::abs(MyMath::dotProduct(dir, MyVector(-dv.y, dv.x))) > 1e-3) continue;

        double dh = MyMath::dotProduct(dm->dimLine, normal) - base;
        if (std::abs(dh - h) > 1e-3) continue;

        double t1 = MyMath::dotProduct(dm->start, dir);
        double t2 = MyMath::dotProduct(dm->end, dir);
        if (t1 < tMin - 1e-3 || t1 > tMax + 1e-3 || t2 < tMin - 1e-3 || t2 > tMax + 1e-3) continue;
        removeList.append(li);
    }

    if (ctx && ctx->undoStack()) {
        ctx->undoStack()->beginMacro(tr("치수병합"));
        for (int i = 0; i < removeList.size(); ++i) {
            ctx->undoStack()->push(new RemoveEntityCommand(m_scene, removeList.at(i)));
        }
        ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
        ctx->undoStack()->endMacro();
    } else {
        item->setParentItem(m_scene->getLayerRoot(a->layerName()));
        for (int i = 0; i < removeList.size(); ++i) {
            m_scene->removeItem(removeList.at(i));
            delete removeList.at(i);
        }
    }

    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->logMessage(tr("치수가 병합되었습니다."), CommandDock::Info);
    }
    finishTool();
}
