#include "autocontinuousdimtool.h"
#include "caditem.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "dimmergetool.h"
#include "iappcontext.h"
#include "layer.h"
#include "layermanager.h"
#include "lineardimensionitem.h"
#include "myline.h"
#include "mymath.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include <QUndoStack>
#include <QtMath>
#include <algorithm>

AutoContinuousDimTool::AutoContinuousDimTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitStartPoint), m_isDrawing(false) {}

AutoContinuousDimTool::~AutoContinuousDimTool() { cancelDrawing(); }

void AutoContinuousDimTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    m_state = WaitStartPoint;
    m_isDrawing = false;
    m_intersectPts.clear();
    m_entityGrips.clear();
    m_ghostNormal = MyVector(0, 1);
    m_baseDimData = MyDimension();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("AUTOCONTINUOUSDIM", CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(tr(" 시작점을 지정 하세요 [1] : "), {});
    }
}

void AutoContinuousDimTool::deactivate() {
    cancelDrawing();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->setToolActive(false);
    AbstractTool::deactivate();
}

void AutoContinuousDimTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;
    IAppContext *ctx = getContext();

    if (button == Qt::RightButton) { onCancel(); return; }
    if (button != Qt::LeftButton)  return;

    QPointF snapped = pos;
    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state != WaitStartPoint) ? m_pt1.toPointF() : QPointF();
        snapped = ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    }

    if (m_state == WaitStartPoint) {
        m_pt1 = MyVector(snapped);
        m_state = WaitEndPoint;
        m_isDrawing = true;
        if (ctx && ctx->commandDock())
            ctx->commandDock()->showInteractivePrompt(tr(" 끝점을 지정 하세요 [2] : "), {});

    } else if (m_state == WaitEndPoint) {
        m_pt2 = MyVector(snapped);
        if (m_pt1.distanceTo(m_pt2) < 0.001) return;

        computeIntersections();

        if (m_intersectPts.size() < 2) {
            if (ctx && ctx->commandDock())
                ctx->commandDock()->logMessage(
                    tr(" 가상선과 교차하는 선이 2개 미만입니다."), CommandDock::Command);
            cancelDrawing();
            return;
        }

        m_state = WaitDimLinePoint;
        if (ctx && ctx->commandDock())
            ctx->commandDock()->showInteractivePrompt(
                tr(" 치수선이 놓일 위치를 지정 하세요 [3] : "), {});

    } else if (m_state == WaitDimLinePoint) {
        finalizeDimensions(snapped);
    }
}

void AutoContinuousDimTool::mouseMove(QPointF pos) {
    m_currentMousePos = pos;
    IAppContext *ctx = getContext();

    QPointF finalPos = pos;
    SnapType snapType = SnapType::None;
    if (ctx && ctx->snapManager()) {
        QPointF base = (m_state != WaitStartPoint) ? m_pt1.toPointF() : QPointF();
        SnapManager::SnapResult res = ctx->snapManager()->snapPoint(pos, m_scene, base);
        finalPos = res.snappedPos;
        snapType = res.type;
    }

    if (m_scene) m_scene->setSnapMarker(finalPos, snapType);
    if (m_isDrawing) updateGhostPreview();
}

void AutoContinuousDimTool::mouseRelease(QPointF pos, Qt::MouseButton button) {
    Q_UNUSED(pos); Q_UNUSED(button);
}

void AutoContinuousDimTool::onFinish() {
    if (m_state == WaitDimLinePoint)
        finalizeDimensions(m_currentMousePos);
}

void AutoContinuousDimTool::onCancel() {
    cancelDrawing();
    if (getContext() && getContext()->commandDock())
        getContext()->commandDock()->setPrompt(" Command : ");
    emit toolFinished();
}

void AutoContinuousDimTool::cancelDrawing() {
    m_isDrawing = false;
    m_state = WaitStartPoint;
    m_intersectPts.clear();
    m_entityGrips.clear();
    m_ghostNormal = MyVector(0, 1);
    m_baseDimData = MyDimension();
    m_chainItems.clear();
    if (m_scene) m_scene->clearOverlay();
}

// 가상선이 수평/수직에서 약간 틀어진 경우(클릭 오차) 축 방향으로 보정한 치수 각도(deg).
// 보정하지 않으면 측정값이 cos만큼 줄고(1499.99 등) 치수선이 P3 거리에 따라 기울어진다.
static double snappedGhostAngle(const MyVector &p1, const MyVector &p2)
{
    double deg = qRadiansToDegrees(qAtan2(p2.y - p1.y, p2.x - p1.x));
    const double tol = 10.0;
    for (int k = -180; k <= 180; k += 90) {
        if (qAbs(deg - k) <= tol) return double(k);
    }
    return deg;
}

// ─────────────────────────────────────────────────────────────────────────────
// 교점 계산 + m_ghostNormal 결정
// m_ghostNormal = 가상선에 수직인 방향 → pickExtOrigin의 측면 결정에 사용
// ─────────────────────────────────────────────────────────────────────────────
void AutoContinuousDimTool::computeIntersections() {
    m_intersectPts.clear();
    m_entityGrips.clear();
    if (!m_scene) return;

    MyVector ghostDir = m_pt2 - m_pt1;
    double ghostLen   = ghostDir.magnitude();
    if (ghostLen < 1e-6) return;

    // 가상선 수직 방향 저장 (pickExtOrigin 측면 결정용)
    m_dimAngle = snappedGhostAngle(m_pt1, m_pt2);
    m_ghostNormal = MyVector(0, 1); // 교점 계산 후 m_dimAngle 확정 시 갱신

    MyLine ghostLine(m_pt1, m_pt2);

    struct HitInfo { double t; QList<QPointF> grips; };
    QList<HitInfo> hits;

    for (QGraphicsItem *gi : m_scene->items()) {
        CadItem *cadItem = dynamic_cast<CadItem *>(gi);
        if (!cadItem) continue;
        const MyEntity *ent = cadItem->getEntityData();
        if (!ent) continue;

        for (const MyVector &p : ghostLine.intersect(ent)) {
            double t = MyMath::dotProduct(p - m_pt1, ghostDir) / (ghostLen * ghostLen);
            if (t < 0.0001 || t > 0.9999) continue;

            bool dup = false;
            for (const HitInfo &ex : hits)
                if (qAbs(ex.t - t) * ghostLen < 0.5) { dup = true; break; }
            if (dup) continue;

            hits.append({t, cadItem->getGripPoints()});
        }
    }

    std::sort(hits.begin(), hits.end(),
              [](const HitInfo &a, const HitInfo &b){ return a.t < b.t; });

    for (const HitInfo &h : hits) {
        m_intersectPts.append(MyVector(m_pt1.x + ghostDir.x * h.t,
                                       m_pt1.y + ghostDir.y * h.t));
        m_entityGrips.append(h.grips);
    }

    // 치수 방향은 "엔티티에 수직"이 정확하다. 가상선이 엔티티와 정확히 직교하지 않으면
    // 가상선 각도를 쓸 때 측정값이 cos만큼 줄어든다(999.97 등).
    if (!m_entityGrips.isEmpty()) {
        const QList<QPointF> &g = m_entityGrips.first();
        double best = 0.0;
        QPointF ea, eb;
        for (int i = 0; i < g.size(); ++i) {
            for (int j = i + 1; j < g.size(); ++j) {
                QPointF d = g.at(j) - g.at(i);
                double len = std::hypot(d.x(), d.y());
                if (len > best) { best = len; ea = g.at(i); eb = g.at(j); }
            }
        }
        if (best > 1e-6) {
            double cand = qRadiansToDegrees(qAtan2(eb.y() - ea.y(), eb.x() - ea.x())) + 90.0;
            double ghostDeg = qRadiansToDegrees(qAtan2(ghostDir.y, ghostDir.x));
            double diff = std::fmod(cand - ghostDeg, 180.0);
            if (diff > 90.0) diff -= 180.0;
            if (diff < -90.0) diff += 180.0;
            if (qAbs(diff) <= 15.0) {
                m_dimAngle = ghostDeg + diff;
                // 수평/수직에 매우 가까우면 축으로 보정
                for (int k = -180; k <= 180; k += 90) {
                    if (qAbs(m_dimAngle - k) <= 0.05) { m_dimAngle = double(k); break; }
                }
            }
        }
    }
    double dimRad = qDegreesToRadians(m_dimAngle);
    m_ghostNormal = MyVector(-qSin(dimRad), qCos(dimRad));
}

// ─────────────────────────────────────────────────────────────────────────────
// 교점 + 엔티티 그립포인트로부터 치수보조선 기점(엔티티 끝점) 선택
// normal  : 어느 방향 측이 치수선 쪽인지 판단하는 방향 벡터
// hSign   : > 0 이면 normal 양방향, < 0 이면 음방향
// ─────────────────────────────────────────────────────────────────────────────
static MyVector pickExtOrigin(const MyVector &intersectPt,
                               const QList<QPointF> &grips,
                               const MyVector &normal, double hSign)
{
    MyVector best = intersectPt;
    double bestDist = 0.0;
    for (const QPointF &gp : grips) {
        MyVector v(gp.x(), gp.y());
        double proj = MyMath::dotProduct(v - intersectPt, normal);
        if (hSign >= 0.0 && proj > 0.0 && proj > bestDist)      { best = v; bestDist =  proj; }
        else if (hSign < 0.0 && proj < 0.0 && proj < bestDist)  { best = v; bestDist =  proj; }
    }
    return best;
}

// ─────────────────────────────────────────────────────────────────────────────
// 치수선 생성
// LinearDimensionItem처럼 useFixedAngle=false → start→end 방향이 치수 방향
// ─────────────────────────────────────────────────────────────────────────────
void AutoContinuousDimTool::finalizeDimensions(const QPointF &dimLinePos) {
    if (!m_scene || m_intersectPts.size() < 2) { onCancel(); return; }
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->layerManager()) { onCancel(); return; }

    m_dimLinePos = MyVector(dimLinePos);

    // 가상선 수직(m_ghostNormal) 기준으로 치수선이 어느 측인지 결정
    double hForSide = MyMath::dotProduct(m_dimLinePos - m_pt1, m_ghostNormal);
    double hSign    = (hForSide >= 0.0) ? 1.0 : -1.0;

    DimensionStyle style = m_scene->dimensionStyle();
    QString targetLayerName = style.dimLineLayer;
    if (targetLayerName.isEmpty() || !ctx->layerManager()->getLayer(targetLayerName))
        targetLayerName = ctx->layerManager()->getCurrentLayer()->name();
    Layer *targetLayer = ctx->layerManager()->getLayer(targetLayerName);

    QPen pen(targetLayer->color());
    pen.setWidthF(targetLayer->weight());

    m_baseDimData.precision                 = style.precision;
    m_baseDimData.suppressTrailingZeros     = style.suppressTrailingZeros;
    m_baseDimData.suppressTrailingZerosTol  = style.suppressTrailingZerosTol;

    for (int i = 0; i < m_intersectPts.size() - 1; ++i) {
        const MyVector &iPtA = m_intersectPts[i];
        const MyVector &iPtB = m_intersectPts[i + 1];

        // 엔티티 끝점(치수보조선 기점) — 가상선 수직 방향으로 치수선 쪽 끝점 선택
        MyVector extA = (i   < m_entityGrips.size())
                        ? pickExtOrigin(iPtA, m_entityGrips[i],   m_ghostNormal, hSign)
                        : iPtA;
        MyVector extB = (i+1 < m_entityGrips.size())
                        ? pickExtOrigin(iPtB, m_entityGrips[i+1], m_ghostNormal, hSign)
                        : iPtB;

        if (extA.distanceTo(extB) < 1e-4) continue;

        MyDimension data = m_baseDimData;
        data.text          = "";
        data.dimensionType = DimensionType::Linear;
        data.start         = extA;
        data.end           = extB;
        // useFixedAngle = false → LinearDimensionItem이 extA→extB 방향을 측정 방향으로 사용
        // 가상선 방향으로 치수 방향 고정 → 단차가 있어도 모든 치수선이 같은 높이(DIMCONTINUE)
        data.useFixedAngle = true;
        data.fixedAngle    = m_dimAngle;
        // 모든 구간 동일 기준점 → 치수선 높이/위치 일치
        data.dimLine       = m_dimLinePos;
        data.hideExt1      = (i > 0);
        data.hideExt2      = false;

        LinearDimensionItem *item = new LinearDimensionItem(data);
        item->setLayerName(targetLayer->name());
        item->setPen(pen);
        item->setGripsVisible(false);
        item->setParentItem(m_scene->getLayerRoot(targetLayer->name()));

        if (ctx->undoStack())
            ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
        else
            m_scene->addItem(item);
        m_chainItems.append(item);
    }

    m_scene->clearOverlay();
    m_state = WaitTotalDimPrompt;

    if (ctx && ctx->commandDock())
        ctx->commandDock()->showInteractivePrompt(
            tr(" 전체 치수선을 그리겠습니까? (Y/N) <Y> : "), {"Y", "N"});
}

// ─────────────────────────────────────────────────────────────────────────────
// Ghost 미리보기: useFixedAngle=false 동일하게 적용
// ─────────────────────────────────────────────────────────────────────────────
void AutoContinuousDimTool::buildDimPreview(const QPointF &dimLinePos,
                                             QPainterPath &outPath) const {
    if (m_intersectPts.size() < 2) return;

    double hForSide = MyMath::dotProduct(MyVector(dimLinePos) - m_pt1, m_ghostNormal);
    double hSign    = (hForSide >= 0.0) ? 1.0 : -1.0;

    for (int i = 0; i < m_intersectPts.size() - 1; ++i) {
        const MyVector &iPtA = m_intersectPts[i];
        const MyVector &iPtB = m_intersectPts[i + 1];

        MyVector extA = (i   < m_entityGrips.size())
                        ? pickExtOrigin(iPtA, m_entityGrips[i],   m_ghostNormal, hSign)
                        : iPtA;
        MyVector extB = (i+1 < m_entityGrips.size())
                        ? pickExtOrigin(iPtB, m_entityGrips[i+1], m_ghostNormal, hSign)
                        : iPtB;

        if (extA.distanceTo(extB) < 1e-4) continue;

        MyDimension data;
        data.dimensionType = DimensionType::Linear;
        data.start         = extA;
        data.end           = extB;
        data.useFixedAngle = true;    // 가상선 방향 고정
        data.fixedAngle    = m_dimAngle;
        data.dimLine       = MyVector(dimLinePos);  // 동일 기준점
        data.hideExt1      = (i > 0);
        data.hideExt2      = false;

        LinearDimensionItem di(data);
        outPath.addPath(di.geometryPath());
    }
}

void AutoContinuousDimTool::updateGhostPreview() {
    if (!m_isDrawing || !m_scene) return;

    QPainterPath path;
    if (m_state == WaitEndPoint) {
        path.moveTo(m_pt1.toPointF());
        path.lineTo(m_currentMousePos);
    } else if (m_state == WaitDimLinePoint) {
        path.moveTo(m_pt1.toPointF());
        path.lineTo(m_pt2.toPointF());
        if (m_intersectPts.size() >= 2)
            buildDimPreview(m_currentMousePos, path);
    }

    m_scene->setGhostPath(path, true, Qt::yellow, 1.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 전체 치수선 (Y/N)
// 전체 치수도 useFixedAngle=false — extFirst→extLast 방향이 측정 방향
// ─────────────────────────────────────────────────────────────────────────────
bool AutoContinuousDimTool::processCommand(const QString &command) {
    QString cmd = command.trimmed().toLower();
    IAppContext *ctx = getContext();

    if (m_state == WaitTotalDimPrompt) {
        if (cmd == "y" || cmd.isEmpty()) {
            if (m_intersectPts.size() >= 2 && ctx && ctx->layerManager()) {
                DimensionStyle style = m_scene->dimensionStyle();
                QString targetLayerName = style.dimLineLayer;
                if (targetLayerName.isEmpty() || !ctx->layerManager()->getLayer(targetLayerName))
                    targetLayerName = ctx->layerManager()->getCurrentLayer()->name();
                Layer *targetLayer = ctx->layerManager()->getLayer(targetLayerName);

                QPen pen(targetLayer->color());
                pen.setWidthF(targetLayer->weight());

                // 전체 치수 끝점: 첫/마지막 교점의 엔티티 끝점
                double hForSide = MyMath::dotProduct(m_dimLinePos - m_pt1, m_ghostNormal);
                double hSign    = (hForSide >= 0.0) ? 1.0 : -1.0;

                MyVector extFirst = (!m_entityGrips.isEmpty())
                    ? pickExtOrigin(m_intersectPts.first(), m_entityGrips.first(), m_ghostNormal, hSign)
                    : m_intersectPts.first();
                MyVector extLast  = (m_entityGrips.size() >= m_intersectPts.size())
                    ? pickExtOrigin(m_intersectPts.last(), m_entityGrips.last(), m_ghostNormal, hSign)
                    : m_intersectPts.last();

                // 전체 치수선 위치: 연속 치수선에서 baselineSpacing 하나 더 바깥
                double spacing = style.baselineSpacing * m_scene->drawingScale();
                MyVector totalDimLine = m_dimLinePos + m_ghostNormal * (hSign * spacing);

                MyDimension totalData = m_baseDimData;
                totalData.text          = "";
                totalData.dimensionType = DimensionType::Linear;
                totalData.start         = extFirst;
                totalData.end           = extLast;
                totalData.useFixedAngle = true;    // 가상선 방향 고정
                totalData.fixedAngle    = m_dimAngle;
                totalData.dimLine       = totalDimLine;
                totalData.hideExt1      = false;
                totalData.hideExt2      = false;

                LinearDimensionItem *item = new LinearDimensionItem(totalData);
                item->setLayerName(targetLayer->name());
                item->setPen(pen);
                item->setGripsVisible(false);
                item->setParentItem(m_scene->getLayerRoot(targetLayer->name()));

                // 전체 치수보조선과 겹치는 기존 치수보조선은 숨김
                if (ctx->undoStack()) ctx->undoStack()->beginMacro(tr("전체 치수선"));
                DimMergeTool::hideCoincidentExtLines(ctx, m_chainItems, extFirst, extLast);
                if (ctx->undoStack()) {
                    ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
                    ctx->undoStack()->endMacro();
                } else {
                    m_scene->addItem(item);
                }
            }
            onCancel();
            return true;
        } else if (cmd == "n") {
            onCancel();
            return true;
        }
    }
    return false;
}
