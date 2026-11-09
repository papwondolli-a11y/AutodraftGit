#include "dimflipsidetool.h"
#include "basedimensionitem.h"
#include "caditem.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "dimmergetool.h"
#include "iappcontext.h"
#include "lineardimensionitem.h"
#include "modifyentitycommand.h"
#include "myline.h"
#include "mydimension.h"
#include "mypolyline.h"
#include "mygraphicsscene.h"
#include "mymath.h"
#include "snapmanager.h"
#include <QMap>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QUndoStack>
#include <QVariant>
#include <QtMath>
#include <cmath>

DimFlipSideTool::DimFlipSideTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitSelect), m_target(nullptr), m_defaultAbs(0.0) {}

DimFlipSideTool::~DimFlipSideTool() { resetTool(); }

void DimFlipSideTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene) return;
    AbstractTool::activate(scene);

    resetTool();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->showInteractivePrompt(tr(" 반대면으로 옮길 치수선을 선택하세요 : "), {});
    }
}

void DimFlipSideTool::deactivate() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(false);
    }
    resetTool();
    AbstractTool::deactivate();
}

void DimFlipSideTool::resetTool() {
    m_state = WaitSelect;
    m_target = nullptr;
    if (m_scene) m_scene->clearOverlay();
}

void DimFlipSideTool::finishTool() {
    resetTool();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setPrompt(" Command : ");
    }
    emit toolFinished();
}

void DimFlipSideTool::onCancel() { finishTool(); }

QPointF DimFlipSideTool::snapped(const QPointF &pos) const {
    IAppContext *ctx = getContext();
    if (ctx && ctx->snapManager() && m_scene) {
        return ctx->snapManager()->snapPoint(pos, m_scene, QPointF()).snappedPos;
    }
    return pos;
}

double DimFlipSideTool::farthestExtent(const MyVector &p, const MyVector &direction) const {
    if (!m_scene) return -1.0;
    const QRectF world = m_scene->itemsBoundingRect();
    const double reach = world.width() + world.height() + 1.0;

    // 정의점에서 대상물 안쪽으로 뻗는 가는 광선
    QPainterPath line;
    line.moveTo(p.x, p.y);
    line.lineTo(p.x + direction.x * reach, p.y + direction.y * reach);
    QPainterPathStroker stroker;
    stroker.setWidth(qMax(reach * 1e-6, 1e-3));
    const QPainterPath ray = stroker.createStroke(line);

    // 도면틀·다른 도형이 광선 위에 있어도 엉뚱한 곳으로 가지 않도록,
    // 정의점에 닿아 있는 도형과 거기에 이어진 도형(같은 대상물)만 대상으로 한다.
    struct Cand { QGraphicsItem *item; QPainterPath shape; QRectF box; };
    QList<Cand> cands;
    const QList<QGraphicsItem *> all = m_scene->items();
    for (int i = 0; i < all.size(); ++i) {
        QGraphicsItem *gi = all.at(i);
        if (dynamic_cast<BaseDimensionItem *>(gi) || gi == m_target) continue; // 치수는 제외
        if (!gi->isVisible()) continue;
        Cand c;
        c.item = gi;
        c.shape = gi->sceneTransform().map(gi->shape());
        if (c.shape.isEmpty()) continue;
        c.box = c.shape.boundingRect();
        cands.append(c);
    }

    const double tol = qMax(reach * 1e-5, 1e-3);
    QPainterPath probe;
    probe.addRect(QRectF(p.x - tol, p.y - tol, tol * 2.0, tol * 2.0));

    QList<int> comp;                    // 대상물에 속한 후보 인덱스
    QList<bool> inComp;
    for (int i = 0; i < cands.size(); ++i) inComp.append(false);
    for (int i = 0; i < cands.size(); ++i) {
        if (cands.at(i).box.intersects(probe.boundingRect()) && cands.at(i).shape.intersects(probe)) {
            inComp[i] = true;
            comp.append(i);
        }
    }
    for (int head = 0; head < comp.size() && comp.size() < 2000; ++head) {
        const Cand &cur = cands.at(comp.at(head));
        for (int j = 0; j < cands.size(); ++j) {
            if (inComp.at(j)) continue;
            if (!cands.at(j).box.intersects(cur.box)) continue;
            if (cands.at(j).shape.intersects(cur.shape)) {
                inComp[j] = true;
                comp.append(j);
            }
        }
    }

    // 광선이 각 도형과 겹치는 구간 [tmin, tmax] (정의점에서의 거리)
    QList<double> lo, hi;
    for (int k = 0; k < comp.size(); ++k) {
        const Cand &c = cands.at(comp.at(k));
        if (!c.shape.intersects(ray)) continue;
        const QRectF r = c.shape.intersected(ray).boundingRect();
        const QPointF corners[4] = {r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight()};
        double tmin = 1e300, tmax = -1e300;
        for (int m = 0; m < 4; ++m) {
            const double t = (corners[m].x() - p.x) * direction.x + (corners[m].y() - p.y) * direction.y;
            tmin = qMin(tmin, t);
            tmax = qMax(tmax, t);
        }
        if (tmax <= -tol) continue;       // 광선 뒤쪽
        lo.append(tmin);
        hi.append(tmax);
    }

    // 1) 정의점에서 시작해 광선을 따라 이어지는 도형(정의점이 면의 모서리 선 위에 있는 경우):
    //    이어지는 구간의 끝이 반대 모서리다.
    double cur = 0.0;
    bool grew = true;
    while (grew) {
        grew = false;
        for (int i = 0; i < lo.size(); ++i) {
            if (lo.at(i) <= cur + tol && hi.at(i) > cur + tol) {
                cur = hi.at(i);
                grew = true;
            }
        }
    }
    if (cur > tol) return cur;

    // 2) 면을 가로지르는 경우: 정의점 너머에서 처음 만나는 면이 반대면이다.
    double first = -1.0;
    for (int i = 0; i < lo.size(); ++i) {
        if (lo.at(i) > tol && (first < 0.0 || lo.at(i) < first)) first = lo.at(i);
    }
    return first;

}

namespace {
// 꼭짓점 목록 하나 (선 = 끝점 2개, 폴리라인 = 모든 정점)
struct VertexSet {
    QList<MyVector> pts;
    bool isLine = false;
};

bool sameVertex(const MyVector &a, const MyVector &b, double tol) {
    return std::abs(a.x - b.x) <= tol && std::abs(a.y - b.y) <= tol;
}

bool hasVertexAt(const VertexSet &vs, const MyVector &p, double tol) {
    for (int i = 0; i < vs.pts.size(); ++i)
        if (sameVertex(vs.pts.at(i), p, tol)) return true;
    return false;
}

bool touchesAny(const VertexSet &vs, const VertexSet &other, double tol) {
    for (int i = 0; i < other.pts.size(); ++i)
        if (hasVertexAt(vs, other.pts.at(i), tol)) return true;
    return false;
}
} // namespace

bool DimFlipSideTool::oppositeByVertex(const MyVector &p, const MyVector &inward, MyVector &out) const {
    if (!m_scene) return false;
    const QRectF world = m_scene->itemsBoundingRect();
    const double tol = qMax((world.width() + world.height()) * 1e-7, 1e-4);

    // 도형 엔티티의 꼭짓점 수집 (치수·기타 항목 제외)
    QList<VertexSet> sets;
    const QList<QGraphicsItem *> all = m_scene->items();
    for (int i = 0; i < all.size(); ++i) {
        CadItem *ci = dynamic_cast<CadItem *>(all.at(i));
        if (!ci || dynamic_cast<BaseDimensionItem *>(ci)) continue;
        const MyEntity *e = ci->getEntityData();
        VertexSet vs;
        if (const MyLine *ln = dynamic_cast<const MyLine *>(e)) {
            vs.isLine = true;
            vs.pts.append(ln->start);
            vs.pts.append(ln->end);
        } else if (const MyPolyline *pl = dynamic_cast<const MyPolyline *>(e)) {
            for (size_t k = 0; k < pl->vertices.size(); ++k) vs.pts.append(pl->vertices[k]);
        } else {
            continue;
        }
        sets.append(vs);
    }

    // 정의점을 꼭짓점으로 가진 엔티티 + (선으로 그린 도형이면) 끝점이 맞닿아 이어진 선들
    QList<int> comp;
    QList<bool> inComp;
    for (int i = 0; i < sets.size(); ++i) inComp.append(false);
    for (int i = 0; i < sets.size(); ++i) {
        if (hasVertexAt(sets.at(i), p, tol)) { inComp[i] = true; comp.append(i); }
    }
    if (comp.isEmpty()) return false;
    for (int head = 0; head < comp.size() && comp.size() < 500; ++head) {
        const VertexSet &cur = sets.at(comp.at(head));
        if (!cur.isLine) continue;               // 폴리라인은 그 자체로 도형이므로 확장하지 않음
        for (int j = 0; j < sets.size(); ++j) {
            if (inComp.at(j) || !sets.at(j).isLine) continue;
            if (touchesAny(sets.at(j), cur, tol)) { inComp[j] = true; comp.append(j); }
        }
    }

    // 후보: p 와 같은 치수방향 좌표(= p 에서 치수선 수직 방향으로 놓인 점)이면서 안쪽에 있는 꼭짓점.
    // 그중 가장 먼 점이 반대면이다.
    const double dtol = qMax(tol, 1e-3);
    double best = -1.0;
    for (int k = 0; k < comp.size(); ++k) {
        const VertexSet &vs = sets.at(comp.at(k));
        for (int i = 0; i < vs.pts.size(); ++i) {
            const MyVector v = vs.pts.at(i) - p;
            if (std::abs(MyMath::dotProduct(v, m_dir)) > dtol) continue;   // 치수 방향 위치가 달라짐
            const double t = MyMath::dotProduct(v, inward);
            if (t > dtol && t > best) {
                best = t;
                out = vs.pts.at(i);
            }
        }
    }
    return best > 0.0;
}

bool DimFlipSideTool::oppositePoint(const MyVector &p, const MyVector &inward, MyVector &out) const {
    if (oppositeByVertex(p, inward, out)) return true;
    const double e = farthestExtent(p, inward);   // 꼭짓점 방식이 안 되면 광선 방식
    if (e <= 1e-6) return false;
    out = p + inward * e;
    return true;
}

bool DimFlipSideTool::prepare(LinearDimensionItem *dim) {
    IAppContext *ctx = getContext();
    CommandDock *dock = ctx ? ctx->commandDock() : nullptr;
    const MyDimension *d = dynamic_cast<const MyDimension *>(dim->getEntityData());
    if (!d || !DimMergeTool::dimDirection(d, m_dir)) return false;
    m_normal = MyVector(-m_dir.y, m_dir.x);

    // 현재 치수선이 있는 쪽(바깥)과 그 반대(대상물 안쪽 → 반대면) 방향
    const double h = MyMath::dotProduct(d->dimLine - d->start, m_normal);
    const double outSign = (h >= 0.0) ? 1.0 : -1.0;
    const MyVector inward = m_normal * (-outSign);

    if (!oppositePoint(d->start, inward, m_newStart) || !oppositePoint(d->end, inward, m_newEnd)) {
        if (dock) dock->logMessage(tr("반대면을 찾지 못했습니다. (치수 기점이 도형 위에 있어야 합니다)"), CommandDock::Info);
        return false;
    }

    // 기본 치수선 위치: 반대면 바깥쪽으로 기존과 같은 거리
    m_defaultAbs = MyMath::dotProduct(m_newStart, m_normal) + (-outSign) * std::abs(h);
    return true;
}

void DimFlipSideTool::updatePreview(double lineAbs) {
    if (!m_scene || !m_target) return;
    const MyDimension *d = dynamic_cast<const MyDimension *>(m_target->getEntityData());
    if (!d) return;
    MyDimension data = *d;
    data.start = m_newStart;
    data.end = m_newEnd;
    data.dimLine = m_newStart + m_normal * (lineAbs - MyMath::dotProduct(m_newStart, m_normal));
    data.useCustomTextPos = false;
    LinearDimensionItem di(data);
    m_scene->setGhostPath(di.geometryPath(), true, Qt::yellow, 1.0);
}

void DimFlipSideTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;
    IAppContext *ctx = getContext();

    if (button == Qt::RightButton) {
        if (m_state == WaitPosition) apply(m_defaultAbs);
        else finishTool();
        return;
    }
    if (button != Qt::LeftButton) return;

    if (m_state == WaitSelect) {
        LinearDimensionItem *dim = dynamic_cast<LinearDimensionItem *>(itemAtPos(pos));
        if (!dim) return;
        m_target = dim;
        if (!prepare(dim)) {
            m_target = nullptr;
            return;
        }
        m_state = WaitPosition;
        updatePreview(m_defaultAbs);
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->showInteractivePrompt(
                tr(" 치수선이 놓일 위치를 지정하세요 (엔터/우클릭:기본 위치) : "), {});
        }
        return;
    }

    if (m_state == WaitPosition) {
        const QPointF p = snapped(pos);
        apply(MyMath::dotProduct(MyVector(p), m_normal));
    }
}

void DimFlipSideTool::mouseMove(QPointF pos) {
    if (m_state != WaitPosition) return;
    const QPointF p = snapped(pos);
    updatePreview(MyMath::dotProduct(MyVector(p), m_normal));
}

bool DimFlipSideTool::processCommand(const QString &command) {
    if (m_state == WaitPosition && command.trimmed().isEmpty()) {
        apply(m_defaultAbs);
        return true;
    }
    return false;
}

void DimFlipSideTool::apply(double lineAbs) {
    IAppContext *ctx = getContext();
    if (!m_target) { finishTool(); return; }
    const MyVector dimLine = m_newStart + m_normal * (lineAbs - MyMath::dotProduct(m_newStart, m_normal));

    QMap<QString, QVariant> oldProps = m_target->getProperties();
    QMap<QString, QVariant> np = oldProps;
    np["StartX"] = m_newStart.x;  np["StartY"] = m_newStart.y;
    np["EndX"] = m_newEnd.x;      np["EndY"] = m_newEnd.y;
    np["DimLineX"] = dimLine.x;   np["DimLineY"] = dimLine.y;
    np["UseCustomTextPos"] = false;

    if (ctx && ctx->undoStack()) {
        ctx->undoStack()->push(new ModifyEntityCommand(m_target, oldProps, np));
    } else {
        const QList<QString> keys = np.keys();
        for (int k = 0; k < keys.size(); ++k) m_target->setProperty(keys.at(k), np.value(keys.at(k)));
    }
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->logMessage(tr("치수를 반대면으로 옮겼습니다."), CommandDock::Info);
    }
    finishTool();
}
