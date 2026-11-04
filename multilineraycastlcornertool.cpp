#include "multilineraycastlcornertool.h"
#include "multilinedata.h"

#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "lineitem.h"
#include "myline.h"
#include "mygraphicsscene.h"

#include <QDebug>
#include <QGraphicsView>
#include <QLineF>
#include <QUndoStack>
#include <algorithm>
#include <limits>
#include <cmath>

// ── ctor ─────────────────────────────────────────────────────────────────────
MultilineRaycastLCornerTool::MultilineRaycastLCornerTool(QObject *parent)
    : AbstractTool(parent)
{}

// ── activate / deactivate ────────────────────────────────────────────────────
void MultilineRaycastLCornerTool::activate(MyGraphicsScene *scene)
{
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    resetState();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("RAYCAST L-CORNER ACTIVE", CommandDock::Command);
    }
    showPrompt();
}

void MultilineRaycastLCornerTool::deactivate()
{
    resetState();
    AbstractTool::deactivate();
}

// ── helpers ───────────────────────────────────────────────────────────────────
void MultilineRaycastLCornerTool::resetState()
{
    m_firstLine = nullptr;
}

void MultilineRaycastLCornerTool::showPrompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    if (!m_firstLine) {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 첫 번째 선을 코너 근처에서 클릭 "), {});
    } else {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 두 번째 선을 코너 근처에서 클릭(첫번째 선과 만나는 선) "), {});
    }
}

QLineF MultilineRaycastLCornerTool::toLineF(LineItem *li) const
{
    MyLine d = li->getLine();
    return QLineF(d.start.toPointF(), d.end.toPointF());
}

// ── 보조 탐색 ─────────────────────────────────────────────────────────────────
LineItem *MultilineRaycastLCornerTool::findNearestMultilineItem(const QPointF &pos) const
{
    if (!m_scene) return nullptr;

    double extTol = 100.0;
    if (!m_scene->views().isEmpty()) {
        double scale = m_scene->views().constFirst()->viewportTransform().m11();
        if (scale > 1e-6) extTol = 100.0 / scale;
    }

    LineItem *best    = nullptr;
    double   bestDist = std::numeric_limits<double>::max();

    for (QGraphicsItem *item : m_scene->items()) {
        LineItem *li = dynamic_cast<LineItem *>(item);
        if (!li) continue;
        if (li->data(ML_DATA_GROUP_ID).toString().isEmpty()) continue;

        const QLineF  q   = toLineF(li);
        const QPointF dir = q.p2() - q.p1();
        const double  len = q.length();
        double dist;
        if (len < 1e-10) {
            dist = QLineF(q.p1(), pos).length();
        } else {
            const QPointF dn = dir / len;
            const QPointF d  = pos - q.p1();
            dist = qAbs(dn.x() * d.y() - dn.y() * d.x());
        }

        if (dist < extTol && dist < bestDist) {
            bestDist = dist;
            best     = li;
        }
    }
    return best;
}

// ── mouse ─────────────────────────────────────────────────────────────────────
void MultilineRaycastLCornerTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene) return;
    IAppContext *ctx = getContext();

    if (button == Qt::RightButton) {
        onCancel();
        return;
    }
    if (button != Qt::LeftButton) return;

    QGraphicsItem *hit = itemAtPos(pos);
    LineItem *li = dynamic_cast<LineItem *>(hit);

    if (!li || li->data(ML_DATA_GROUP_ID).toString().isEmpty()) {
        if (LineItem *mlLi = findNearestMultilineItem(pos))
            li = mlLi;
    }

    if (!li) {
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(
                tr("선 객체를 클릭하세요."), CommandDock::Info);
        return;
    }

    QString gid = li->data(ML_DATA_GROUP_ID).toString();

    if (!m_firstLine) {
        m_firstLine  = li;
        m_firstClick = pos;
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(
                gid.isEmpty() ? tr("1선택: 일반 선")
                              : tr("1선택: 그룹 [%1...]").arg(gid.left(8)),
                CommandDock::Info);
        showPrompt();
    } else {
        QString gidA = m_firstLine->data(ML_DATA_GROUP_ID).toString();
        if (!gid.isEmpty() && !gidA.isEmpty() && gid == gidA) {
            if (ctx && ctx->commandDock())
                ctx->commandDock()->logMessage(
                    tr("같은 그룹입니다. 다른 선을 선택하세요."),
                    CommandDock::Info);
            return;
        }

        doCornerJoin(m_firstLine, m_firstClick, li, pos);
        resetState();
        showPrompt();
    }
}

void MultilineRaycastLCornerTool::mouseMove(QPointF /*pos*/)  {}
void MultilineRaycastLCornerTool::mouseRelease(QPointF /*pos*/, Qt::MouseButton /*button*/) {}

bool MultilineRaycastLCornerTool::processCommand(const QString & /*command*/)
{
    return false;
}

// ── onFinish / onCancel ───────────────────────────────────────────────────────
void MultilineRaycastLCornerTool::onFinish()
{
    resetState();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->setPrompt(" Command : ");
    emit toolFinished();
}

void MultilineRaycastLCornerTool::onCancel()
{
    if (m_firstLine) {
        resetState();
        showPrompt();
        IAppContext *ctx = getContext();
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(tr("선택 취소. 다시 클릭하세요."), CommandDock::Info);
    } else {
        onFinish();
    }
}

// ── doCornerJoin ──────────────────────────────────────────────────────────────
void MultilineRaycastLCornerTool::doCornerJoin(LineItem *lineA, const QPointF &clickA,
                                               LineItem *lineB, const QPointF &clickB)
{
    if (!m_scene) return;

    const QString gidA = lineA->data(ML_DATA_GROUP_ID).toString();
    const QString gidB = lineB->data(ML_DATA_GROUP_ID).toString();

    // ── 그룹별 선 수집 ─────────────────────────────────────────────────────────
    auto collectRegularGroup = [this](LineItem *ref, const QPointF &click)
        -> QList<LineItem *> {
        QList<LineItem *> group;
        group << ref;
        if (!m_scene) return group;
        double extTol = 500.0;
        if (!m_scene->views().isEmpty()) {
            double scale = m_scene->views().constFirst()->viewportTransform().m11();
            if (scale > 1e-6) extTol = qMax(500.0, 500.0 / scale);
        }
        const QLineF  refF   = toLineF(ref);
        const double  refLen = refF.length();
        if (refLen < 1e-10) return group;
        const QPointF dn = (refF.p2() - refF.p1()) / refLen;
        for (QGraphicsItem *item : m_scene->items()) {
            auto *li = dynamic_cast<LineItem *>(item);
            if (!li || li == ref) continue;
            if (!li->data(ML_DATA_GROUP_ID).toString().isEmpty()) continue;
            const QLineF  q    = toLineF(li);
            const double  len2 = q.length();
            if (len2 < 1e-10) continue;
            const QPointF dn2  = (q.p2() - q.p1()) / len2;
            if (qAbs(QPointF::dotProduct(dn, dn2)) < 0.9999) continue;
            
            const QPointF dlines  = q.p1() - refF.p1();
            const double  spacing = qAbs(dn.x() * dlines.y() - dn.y() * dlines.x());
            if (spacing > extTol) continue;

            const double cp    = QPointF::dotProduct(click, dn2);
            const double pp1   = QPointF::dotProduct(q.p1(), dn2);
            const double pp2   = QPointF::dotProduct(q.p2(), dn2);
            const double lo    = qMin(pp1, pp2), hi = qMax(pp1, pp2);
            const double projD = (cp < lo) ? (lo - cp) : (cp > hi) ? (cp - hi) : 0.0;
            if (projD > extTol) continue;
            group << li;
        }
        return group;
    };

    QList<LineItem *> groupA, groupB;
    if (gidA.isEmpty()) groupA = collectRegularGroup(lineA, clickA);
    if (gidB.isEmpty()) groupB = collectRegularGroup(lineB, clickB);
    for (QGraphicsItem *item : m_scene->items()) {
        auto *li = dynamic_cast<LineItem *>(item);
        if (!li) continue;
        const QString gid  = li->data(ML_DATA_GROUP_ID).toString();
        const QString role = li->data(ML_DATA_ROLE).toString();
        if (role == "center") continue;
        if (!gid.isEmpty() && gid == gidA)      groupA << li;
        else if (!gid.isEmpty() && gid == gidB) groupB << li;
    }

    if (groupA.isEmpty() || groupB.isEmpty()) {
        IAppContext *ctx = getContext();
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(
                tr("L-Corner: 대상 선을 찾을 수 없습니다."), CommandDock::Info);
        return;
    }

    auto applyDistanceFilter = [this](QList<LineItem *> &grp, LineItem *ref) {
        if (grp.isEmpty() || !ref) return;
        const QLineF  refF = toLineF(ref);
        const double  refLen = refF.length();
        if (refLen < 1e-10) return;
        const QPointF dn = (refF.p2() - refF.p1()) / refLen;
        
        double totalWidth = ref->data(ML_DATA_SPACING).toDouble();
        if (totalWidth < 1e-5) totalWidth = 400.0;
        const double distTh = qMax(totalWidth * 1.5, 100.0);

        QList<LineItem *> out;
        for (int k = 0; k < grp.size(); ++k) {
            LineItem *li = grp.at(k);
            if (li == ref) {
                out << li;
                continue;
            }
            const QLineF  q = toLineF(li);
            const QPointF dlines = q.p1() - refF.p1();
            const double  spacing = qAbs(dn.x() * dlines.y() - dn.y() * dlines.x());
            if (spacing <= distTh) {
                out << li;
            }
        }
        if (!out.isEmpty()) grp = out;
    };

    applyDistanceFilter(groupA, lineA);
    applyDistanceFilter(groupB, lineB);

    // ── 물리적 연장 거리 계산 헬퍼 람다 ──
    auto getExtLen = [](const QPointF &pt, const QLineF &seg) -> double {
        const double l = seg.length();
        if (l < 1e-9) return QLineF(seg.p1(), pt).length();
        const double t = QPointF::dotProduct(pt - seg.p1(), seg.p2() - seg.p1()) / (l * l);
        if (t >= 0.0 && t <= 1.0) return 0.0;
        else if (t < 0.0) return qAbs(t) * l;
        else return (t - 1.0) * l;
    };

    // ── 방향 벡터 ────────────────────────────────────────────────────────────
    const QLineF  refAF = toLineF(lineA);
    const QLineF  refBF = toLineF(lineB);
    const QPointF aDir  = refAF.p2() - refAF.p1();
    const QPointF bDir  = refBF.p2() - refBF.p1();

    // ── 평행성 방향 필터: 각 그룹에서 기준선과 평행하지 않은(꺾인) 세그먼트 배제 ──
    auto applyParallelFilter = [this](QList<LineItem *> &grp, const QPointF &dir) {
        const double dirLen = QLineF(QPointF(0, 0), dir).length();
        if (dirLen < 1e-10) return;
        const QPointF dn = dir / dirLen;
        constexpr double parallelTh = 0.95;

        QList<LineItem *> out;
        for (int k = 0; k < grp.size(); ++k) {
            LineItem *li = grp.at(k);
            const QLineF  q   = toLineF(li);
            const double  l   = q.length();
            if (l < 1e-10) continue;
            const double  dot = qAbs(QPointF::dotProduct(dn, (q.p2() - q.p1()) / l));
            if (dot >= parallelTh) {
                out << li;
            }
        }
        if (!out.isEmpty()) grp = out;
    };

    applyParallelFilter(groupA, aDir);
    applyParallelFilter(groupB, bDir);

    // ── 수직 거리 정렬 ────────────────────────────────────────────────────────
    auto perpDist = [this](LineItem *li, const QPointF &click) -> double {
        const QLineF  q   = toLineF(li);
        const QPointF dir = q.p2() - q.p1();
        const double  len = q.length();
        if (len < 1e-10)
            return QLineF(q.p1(), click).length();
        const QPointF dn = dir / len;
        const QPointF d  = click - q.p1();
        return qAbs(dn.x() * d.y() - dn.y() * d.x());
    };

    std::sort(groupA.begin(), groupA.end(), [&](LineItem *a, LineItem *b) {
        return perpDist(a, clickA) < perpDist(b, clickA);
    });
    std::sort(groupB.begin(), groupB.end(), [&](LineItem *a, LineItem *b) {
        return perpDist(a, clickB) < perpDist(b, clickB);
    });

    if (gidA.isEmpty() && groupA.size() > 2) {
        groupA = groupA.mid(0, 2);
    }
    if (gidB.isEmpty() && groupB.size() > 2) {
        groupB = groupB.mid(0, 2);
    }

    // ── 일반선 임시 역할 부여 ───────────────────────────────────────────────────
    auto assignVirtualRoles = [](QList<LineItem *> &grp, const QPointF &perpVec) {
        if (grp.isEmpty()) return;
        QList<QPair<double, LineItem *>> sorted;
        for (int i = 0; i < grp.size(); ++i) {
            LineItem *li = grp.at(i);
            const QLineF q = QLineF(li->getLine().start.toPointF(), li->getLine().end.toPointF());
            const QPointF m = (q.p1() + q.p2()) / 2.0;
            const double prj = QPointF::dotProduct(m, perpVec);
            sorted << qMakePair(prj, li);
        }
        std::sort(sorted.begin(), sorted.end(), [](const QPair<double, LineItem *>& a, const QPair<double, LineItem *>& b) {
            return a.first < b.first;
        });
        const int n = sorted.size();
        for (int i = 0; i < n; ++i) {
            LineItem *li = sorted.at(i).second;
            if (!li->data(ML_DATA_GROUP_ID).toString().isEmpty()) {
                if (!li->data(ML_DATA_ROLE).toString().isEmpty()) continue;
            }
            if (n == 1) {
                li->setData(ML_DATA_ROLE, "outer");
            } else if (n == 2) {
                li->setData(ML_DATA_ROLE, "outer");
            } else if (n == 3) {
                if (i == 0 || i == 2) li->setData(ML_DATA_ROLE, "outer");
                else li->setData(ML_DATA_ROLE, "mortar");
            } else if (n == 4) {
                if (i == 0 || i == 3) li->setData(ML_DATA_ROLE, "outer");
                else li->setData(ML_DATA_ROLE, "mortar");
            } else if (n >= 5) {
                if (i == 0 || i == n - 1) li->setData(ML_DATA_ROLE, "outer");
                else if (i == 1 || i == n - 2) li->setData(ML_DATA_ROLE, "mortar");
                else li->setData(ML_DATA_ROLE, "space");
            }
        }
    };

    const QPointF aPerp(-aDir.y(), aDir.x());
    const QPointF bPerp(-bDir.y(), bDir.x());
    assignVirtualRoles(groupA, aPerp);
    assignVirtualRoles(groupB, bPerp);

    // ── 광선 발사 기반 코너 조인 연산 ───────────────────────────────────────────
    struct TrimOp {
        LineItem *line;
        QPointF   oldPos;
        QPointF   newPos;
        bool      isFirstEnd;
    };
    QList<TrimOp> ops;

    auto getIntersection = [](const QLineF &l1, const QLineF &l2, QPointF *rP) -> bool {
        const double denom = (l2.y2() - l2.y1()) * (l1.x2() - l1.x1()) - (l2.x2() - l2.x1()) * (l1.y2() - l1.y1());
        if (qAbs(denom) < 1e-9) return false;
        const double num = (l2.x2() - l2.x1()) * (l1.y1() - l2.y1()) - (l2.y2() - l2.y1()) * (l1.x1() - l2.x1());
        const double t = num / denom;
        if (rP) *rP = QPointF(l1.x1() + t * (l1.x2() - l1.x1()), l1.y1() + t * (l1.y2() - l1.y1()));
        return true;
    };

    const int n = qMin(groupA.size(), groupB.size());
    for (int i = 0; i < n; ++i) {
        LineItem *la = groupA.at(i);
        LineItem *lb = groupB.at(i);
        const QLineF qla = toLineF(la);
        const QLineF qlb = toLineF(lb);

        QPointF P;
        if (!getIntersection(qla, qlb, &P)) continue;

        // la 트림 방향 결정
        const double pA1    = QPointF::dotProduct(qla.p1(), aDir);
        const double pA2    = QPointF::dotProduct(qla.p2(), aDir);
        const double cpA_cl = QPointF::dotProduct(clickA,   aDir);
        const double cpA_P  = QPointF::dotProduct(P,        aDir);
        const bool   aAtP1  = (pA1 < pA2) == (cpA_cl > cpA_P);
        const QPointF trimEndA    = aAtP1 ? qla.p1() : qla.p2();
        const QPointF nonTrimEndA = aAtP1 ? qla.p2() : qla.p1();

        // lb 트림 방향 결정
        const double pB1    = QPointF::dotProduct(qlb.p1(), bDir);
        const double pB2    = QPointF::dotProduct(qlb.p2(), bDir);
        const double cpB_cl = QPointF::dotProduct(clickB,   bDir);
        const double cpB_P  = QPointF::dotProduct(P,        bDir);
        const bool   bAtP1  = (pB1 < pB2) == (cpB_cl > cpB_P);
        const QPointF trimEndB    = bAtP1 ? qlb.p1() : qlb.p2();
        const QPointF nonTrimEndB = bAtP1 ? qlb.p2() : qlb.p1();

        // [광선 조건 1] la 가 교점 P 방향으로 뻗는지 전방 판정
        const QPointF rayDirA = trimEndA - nonTrimEndA;
        const double  rayLenA = QLineF(QPointF(0,0), rayDirA).length();
        if (rayLenA < 1e-10) continue;
        const QPointF rayDnA  = rayDirA / rayLenA;
        const double  tRayA   = QPointF::dotProduct(P - nonTrimEndA, rayDnA);
        if (tRayA < -10.0) continue;

        // [광선 조건 2] lb 가 교점 P 방향으로 뻗는지 전방 판정
        const QPointF rayDirB = trimEndB - nonTrimEndB;
        const double  rayLenB = QLineF(QPointF(0,0), rayDirB).length();
        if (rayLenB < 1e-10) continue;
        const QPointF rayDnB  = rayDirB / rayLenB;
        const double  tRayB   = QPointF::dotProduct(P - nonTrimEndB, rayDnB);
        if (tRayB < -10.0) continue;

        // [광선 조건 3] 스냅 오차 범위를 고려해 연장 범위 필터링
        const double lenB = qlb.length();
        if (lenB > 1e-9) {
            const double tB = QPointF::dotProduct(P - qlb.p1(), qlb.p2() - qlb.p1()) / (lenB * lenB);
            if (tB < -3.0 || tB > 4.0) continue;
        }
        const double lenA = qla.length();
        if (lenA > 1e-9) {
            const double tA = QPointF::dotProduct(P - qla.p1(), qla.p2() - qla.p1()) / (lenA * lenA);
            if (tA < -3.0 || tA > 4.0) continue;
        }
        // [물리적 연장 길이 필터] 끝점에서 교점 P까지의 연장 길이가 너무 멀면 제외 (최대 300px)
        if (getExtLen(P, qla) > 300.0 || getExtLen(P, qlb) > 300.0) continue;


        ops << TrimOp{ la, trimEndA, P, aAtP1 };
        ops << TrimOp{ lb, trimEndB, P, bAtP1 };
    }

    // ── Undo/Redo 적용 ────────────────────────────────────────────────────────
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->undoStack() || ops.isEmpty()) {
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(
                tr("Raycast L-Corner: 교점을 찾을 수 없습니다."), CommandDock::Info);
        return;
    }

    ctx->undoStack()->beginMacro("Multiline Raycast L-Corner");
    for (int i = 0; i < ops.size(); ++i) {
        const TrimOp &op = ops.at(i);
        ctx->undoStack()->push(
            new ExtendEntityCommand(op.line, op.oldPos, op.newPos, op.isFirstEnd));
    }
    ctx->undoStack()->endMacro();

    if (ctx->commandDock())
        ctx->commandDock()->logMessage(
            tr("Raycast L-Corner 완료: %1개 선 정리됨.").arg(ops.size()),
            CommandDock::Info);
}

// ── MultilineRaycastLCornerCommand ──────────────────────────────────────────
MultilineRaycastLCornerCommand::MultilineRaycastLCornerCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx)
{}

void MultilineRaycastLCornerCommand::execute()
{
    if (!m_ctx) return;
    m_ctx->startTool(new MultilineRaycastLCornerTool());
}
