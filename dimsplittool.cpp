#include "dimsplittool.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "dimmergetool.h"
#include "iappcontext.h"
#include "lineardimensionitem.h"
#include "mydimension.h"
#include "mygraphicsscene.h"
#include "mymath.h"
#include "snapmanager.h"
#include <QUndoStack>
#include <QtMath>
#include <algorithm>
#include <cmath>

// 분리점을 치수 방향(m_dir) 투영값 기준으로 정렬하기 위한 비교자 (람다 대신 함수 객체)
namespace {
struct ProjectionLess {
    MyVector dir;
    bool operator()(const MyVector &a, const MyVector &b) const {
        return MyMath::dotProduct(a, dir) < MyMath::dotProduct(b, dir);
    }
};
} // namespace

DimSplitTool::DimSplitTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitSelect), m_target(nullptr),
      m_keepOriginal(false), m_tStart(0.0), m_tEnd(0.0) {}

DimSplitTool::~DimSplitTool() { resetTool(); }

void DimSplitTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene) return;
    AbstractTool::activate(scene);

    resetTool();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->showInteractivePrompt(tr(" 분리할 치수선을 선택하세요 : "), {});
    }
}

void DimSplitTool::deactivate() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(false);
    }
    resetTool();
    AbstractTool::deactivate();
}

void DimSplitTool::resetTool() {
    m_state = WaitSelect;
    m_target = nullptr;
    m_points.clear();
    m_keepOriginal = false;
    if (m_scene) m_scene->clearOverlay();
}

void DimSplitTool::finishTool() {
    resetTool();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setPrompt(" Command : ");
    }
    emit toolFinished();
}

void DimSplitTool::onCancel() { finishTool(); }

QPointF DimSplitTool::snapped(const QPointF &pos) const {
    IAppContext *ctx = getContext();
    if (ctx && ctx->snapManager() && m_scene) {
        return ctx->snapManager()->snapPoint(pos, m_scene, QPointF()).snappedPos;
    }
    return pos;
}

void DimSplitTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;
    IAppContext *ctx = getContext();

    if (button == Qt::RightButton) {
        if (m_state == WaitPoints) execute();
        else finishTool();
        return;
    }
    if (button != Qt::LeftButton) return;

    if (m_state == WaitSelect) {
        LinearDimensionItem *dim = dynamic_cast<LinearDimensionItem *>(itemAtPos(pos));
        if (!dim) return;
        const MyDimension *d = dynamic_cast<const MyDimension *>(dim->getEntityData());
        if (!d || !DimMergeTool::dimDirection(d, m_dir)) return;

        // 방향은 start -> end 쪽이 + 가 되도록
        if (MyMath::dotProduct(d->end - d->start, m_dir) < 0) m_dir = m_dir * -1.0;
        m_normal = MyVector(-m_dir.y, m_dir.x);
        m_tStart = MyMath::dotProduct(d->start, m_dir);
        m_tEnd = MyMath::dotProduct(d->end, m_dir);
        if (m_tEnd < m_tStart) std::swap(m_tStart, m_tEnd);

        m_target = dim;
        m_points.clear();
        m_state = WaitPoints;
        m_scene->setGhostPath(dim->geometryPath(), true, Qt::cyan, 1.0);
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(
                tr(" 분리 치수보조선이 놓일 위치를 지정하세요 [N:기존 치수선 보존] (종료:엔터,스페이스,마우스우측) : "),
                {"n"});
        }
        return;
    }

    if (m_state == WaitPoints) {
        if (!addPoint(MyVector(snapped(pos))) && ctx && ctx->commandDock()) {
            ctx->commandDock()->logMessage(tr("치수선 범위 안쪽에 지정해야 합니다."), CommandDock::Info);
        }
        updatePreview(pos);
    }
}

void DimSplitTool::mouseMove(QPointF pos) {
    if (m_state == WaitPoints) updatePreview(pos);
}

bool DimSplitTool::addPoint(const MyVector &p) {
    double t = MyMath::dotProduct(p, m_dir);
    const double eps = 1e-3;
    if (t <= m_tStart + eps || t >= m_tEnd - eps) return false;
    for (int i = 0; i < m_points.size(); ++i) {
        if (std::abs(MyMath::dotProduct(m_points.at(i), m_dir) - t) < eps) return false;
    }
    m_points.append(p);
    return true;
}

QList<MyDimension> DimSplitTool::buildSegments(const MyVector *extraPoint) const {
    QList<MyDimension> result;
    if (!m_target) return result;
    const MyDimension *d = dynamic_cast<const MyDimension *>(m_target->getEntityData());
    if (!d) return result;

    // 시작 기점 -> 분리점들 -> 끝 기점 (치수 방향 + 쪽이 뒤)
    MyVector first = d->start, last = d->end;
    if (MyMath::dotProduct(d->end - d->start, m_dir) < 0) std::swap(first, last);

    QList<MyVector> pts = m_points;
    if (extraPoint) {
        double t = MyMath::dotProduct(*extraPoint, m_dir);
        bool ok = t > m_tStart + 1e-3 && t < m_tEnd - 1e-3;
        for (int i = 0; ok && i < pts.size(); ++i) {
            if (std::abs(MyMath::dotProduct(pts.at(i), m_dir) - t) < 1e-3) ok = false;
        }
        if (ok) pts.append(*extraPoint);
    }
    ProjectionLess less;
    less.dir = m_dir;
    std::sort(pts.begin(), pts.end(), less);

    QList<MyVector> all;
    all.append(first);
    all.append(pts);
    all.append(last);

    double hOrig = MyMath::dotProduct(d->dimLine - d->start, m_normal);
    double sign = (hOrig >= 0) ? 1.0 : -1.0;
    double offset = 0.0;
    if (m_keepOriginal && m_scene) {
        // 기존 치수(토탈)를 남기므로 분리 치수는 치수선 간격만큼 안쪽(대상물 쪽)에 배치
        offset = -sign * m_scene->dimensionStyle().baselineSpacing * m_scene->drawingScale();
    }
    double lineAbs = MyMath::dotProduct(d->dimLine, m_normal) + offset;

    for (int i = 0; i + 1 < all.size(); ++i) {
        MyDimension seg = *d;
        seg.text = "";
        seg.start = all.at(i);
        seg.end = all.at(i + 1);
        seg.dimLine = seg.start + m_normal * (lineAbs - MyMath::dotProduct(seg.start, m_normal));
        seg.useFixedAngle = true;
        seg.fixedAngle = qRadiansToDegrees(qAtan2(m_dir.y, m_dir.x));
        seg.useCustomTextPos = false;
        seg.flipArrow1 = false;
        seg.flipArrow2 = false;
        seg.extChainH1 = 0.0;
        seg.extChainH2 = 0.0;
        // 분리점의 치수보조선은 한 번만 그린다: 첫 구간만 시작 보조선, 나머지는 숨김
        seg.hideExt1 = (i > 0);
        seg.hideExt2 = false;
        bool isLast = (i + 2 == all.size());
        if (m_keepOriginal) {
            // 토탈 치수의 양 끝 보조선과 겹치는 분리 치수의 보조선은 숨김
            if (i == 0) seg.hideExt1 = true;
            if (isLast) seg.hideExt2 = true;
        } else {
            if (i == 0) seg.hideExt1 = d->hideExt1;
            if (isLast) seg.hideExt2 = d->hideExt2;
        }
        result.append(seg);
    }
    return result;
}

void DimSplitTool::updatePreview(const QPointF &mouse) {
    if (!m_scene || !m_target) return;
    MyVector cur(snapped(mouse));
    QPainterPath path = m_target->geometryPath();
    QList<MyDimension> segs = buildSegments(&cur);
    for (int i = 0; i < segs.size(); ++i) {
        LinearDimensionItem di(segs.at(i));
        path.addPath(di.geometryPath());
    }
    m_scene->setGhostPath(path, true, Qt::yellow, 1.0);
}

bool DimSplitTool::processCommand(const QString &command) {
    QString cmd = command.trimmed().toLower();
    IAppContext *ctx = getContext();

    if (m_state == WaitPoints) {
        if (cmd == "n") {
            m_keepOriginal = !m_keepOriginal;
            if (ctx && ctx->commandDock()) {
                ctx->commandDock()->logMessage(
                    m_keepOriginal ? tr("기존 치수선을 보존합니다.") : tr("기존 치수선을 삭제하고 분리합니다."),
                    CommandDock::Info);
            }
            return true;
        }
        if (cmd.isEmpty()) {
            execute();
            return true;
        }
    }
    return false;
}

void DimSplitTool::execute() {
    IAppContext *ctx = getContext();
    if (m_points.isEmpty() || !m_target) {
        finishTool();
        return;
    }

    QList<MyDimension> segs = buildSegments(nullptr);
    LinearDimensionItem *orig = m_target;
    QString layerName = orig->layerName();
    QPen pen = orig->pen();

    bool useUndo = ctx && ctx->undoStack();
    if (useUndo) ctx->undoStack()->beginMacro(tr("치수분리"));
    if (!m_keepOriginal) {
        if (useUndo) {
            ctx->undoStack()->push(new RemoveEntityCommand(m_scene, orig));
        } else {
            m_scene->removeItem(orig);
            delete orig;
        }
    }
    for (int i = 0; i < segs.size(); ++i) {
        LinearDimensionItem *item = new LinearDimensionItem(segs.at(i));
        item->setLayerName(layerName);
        item->setPen(pen);
        item->setGripsVisible(false);
        if (useUndo) {
            ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
        } else {
            item->setParentItem(m_scene->getLayerRoot(layerName));
        }
    }
    if (useUndo) ctx->undoStack()->endMacro();

    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->logMessage(tr("치수가 분리되었습니다."), CommandDock::Info);
    }
    m_target = nullptr; // 삭제되었을 수 있으므로 finishTool 의 오버레이 정리 전에 해제
    finishTool();
}
