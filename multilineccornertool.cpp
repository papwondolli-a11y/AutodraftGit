#include "multilineccornertool.h"
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
#include <limits>

// ── ctor ─────────────────────────────────────────────────────────────────────
MultilineCCornerTool::MultilineCCornerTool(QObject *parent)
    : AbstractTool(parent)
{}

// ── activate / deactivate ────────────────────────────────────────────────────
void MultilineCCornerTool::activate(MyGraphicsScene *scene)
{
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    resetState();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("C-CORNER", CommandDock::Command);
    }
    showPrompt();
}

void MultilineCCornerTool::deactivate()
{
    resetState();
    AbstractTool::deactivate();
}

// ── helpers ───────────────────────────────────────────────────────────────────
void MultilineCCornerTool::resetState()
{
    m_firstLine = nullptr;
}

void MultilineCCornerTool::showPrompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    if (!m_firstLine) {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 첫 번째 선을 교차 근처에서 클릭 "), {});
    } else {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 두 번째 선을 교차 근처에서 클릭 "), {});
    }
}

QLineF MultilineCCornerTool::toLineF(LineItem *li) const
{
    MyLine d = li->getLine();
    return QLineF(d.start.toPointF(), d.end.toPointF());
}

LineItem *MultilineCCornerTool::findNearestMultilineItem(const QPointF &pos) const
{
    if (!m_scene) return nullptr;

    double extTol = 100.0;
    if (!m_scene->views().isEmpty()) {
        double scale = m_scene->views().constFirst()->viewportTransform().m11();
        if (scale > 1e-6) extTol = 100.0 / scale;
    }

    LineItem *best    = nullptr;
    double    bestDist = std::numeric_limits<double>::max();

    for (QGraphicsItem *item : m_scene->items()) {
        LineItem *li = dynamic_cast<LineItem *>(item);
        if (!li) continue;

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
void MultilineCCornerTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene) return;
    IAppContext *ctx = getContext();

    if (button == Qt::RightButton) { onCancel(); return; }
    if (button != Qt::LeftButton)  return;

    QGraphicsItem *hit = itemAtPos(pos);
    LineItem *li = dynamic_cast<LineItem *>(hit);
    if (!li)
        li = findNearestMultilineItem(pos);

    if (!li) {
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(
                tr("선 객체를 클릭하세요."), CommandDock::Info);
        return;
    }

    const QString gid = li->data(ML_DATA_GROUP_ID).toString();

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
        const QString gidFirst = m_firstLine->data(ML_DATA_GROUP_ID).toString();
        if (!gid.isEmpty() && !gidFirst.isEmpty() && gid == gidFirst) {
            if (ctx && ctx->commandDock())
                ctx->commandDock()->logMessage(
                    tr("같은 그룹입니다. 다른 선을 선택하세요."),
                    CommandDock::Info);
            return;
        }
        doCrossJoin(m_firstLine, m_firstClick, li, pos);
        resetState();
        showPrompt();
    }
}

void MultilineCCornerTool::mouseMove(QPointF /*pos*/)  {}
void MultilineCCornerTool::mouseRelease(QPointF /*pos*/, Qt::MouseButton /*button*/) {}

bool MultilineCCornerTool::processCommand(const QString & /*command*/)
{
    return false;
}

// ── onFinish / onCancel ───────────────────────────────────────────────────────
void MultilineCCornerTool::onFinish()
{
    resetState();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->setPrompt(" Command : ");
    emit toolFinished();
}

void MultilineCCornerTool::onCancel()
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

// ── collectSplitOps ───────────────────────────────────────────────────────────
//
// group 의 각 선을 other 의 same-role 두 선(좌/우, perpOfOther 기준) 교점에서 분할.
// same-role 이 없으면 최외곽 other 선으로 fallback.
//
void MultilineCCornerTool::collectSplitOps(
    const QList<LineItem *> &group,
    const QList<LineItem *> &other,
    const QPointF           &perpOfOther,
    QList<SplitOp>          &ops)
{
    for (LineItem *line : group) {
        const QString role = line->data(ML_DATA_ROLE).toString();
        const QLineF  ql   = toLineF(line);

        // same-role other 선 중 perpOfOther 투영 min(sameL) / max(sameR) 탐색
        LineItem *sameL = nullptr, *sameR = nullptr;
        double    mnP   =  std::numeric_limits<double>::max();
        double    mxP   = -std::numeric_limits<double>::max();
        for (LineItem *ol : other) {
            if (ol->data(ML_DATA_ROLE).toString() != role) continue;
            const QPointF sm  = (toLineF(ol).p1() + toLineF(ol).p2()) / 2.0;
            const double  prj = QPointF::dotProduct(sm, perpOfOther);
            if (prj < mnP) { mnP = prj; sameL = ol; }
            if (prj > mxP) { mxP = prj; sameR = ol; }
        }
        // fallback: 최외곽 other 선
        if (!sameL || !sameR || sameL == sameR) {
            sameL = nullptr; sameR = nullptr;
            mnP =  std::numeric_limits<double>::max();
            mxP = -std::numeric_limits<double>::max();
            for (LineItem *ol : other) {
                const QPointF sm  = (toLineF(ol).p1() + toLineF(ol).p2()) / 2.0;
                const double  prj = QPointF::dotProduct(sm, perpOfOther);
                if (prj < mnP) { mnP = prj; sameL = ol; }
                if (prj > mxP) { mxP = prj; sameR = ol; }
            }
        }
        if (!sameL || !sameR || sameL == sameR) continue;

        QPointF P_L, P_R;
        if (ql.intersects(toLineF(sameL), &P_L) == QLineF::NoIntersection) continue;
        if (ql.intersects(toLineF(sameR), &P_R) == QLineF::NoIntersection) continue;

        const QPointF lineDir = ql.p2() - ql.p1();
        const double  lenSq   = QPointF::dotProduct(lineDir, lineDir);
        if (lenSq < 1e-10) continue;

        const double tL = QPointF::dotProduct(P_L - ql.p1(), lineDir) / lenSq;
        const double tR = QPointF::dotProduct(P_R - ql.p1(), lineDir) / lenSq;

        constexpr double eps = 1e-6;
        if (qMin(tL, tR) < -eps || qMax(tL, tR) > 1.0 + eps) continue;
        if (QLineF(P_L, P_R).length() < 1e-6) continue;

        const QPointF cutNear = (tL <= tR) ? P_L : P_R;
        const QPointF cutFar  = (tL <= tR) ? P_R : P_L;
        const QPointF origP2  = ql.p2();

        LineItem *nl = new LineItem(MyVector(cutFar), MyVector(origP2));
        nl->setPen(line->pen());
        nl->setLayerName(line->layerName());
        nl->setData(ML_DATA_GROUP_ID, line->data(ML_DATA_GROUP_ID));
        nl->setData(ML_DATA_ROLE,     line->data(ML_DATA_ROLE));

        ops << SplitOp{ line, origP2, cutNear, nl };
    }
}

// ── doCrossJoin ───────────────────────────────────────────────────────────────
//
// C-코너 처리 원리:
//   groupA 와 groupB 의 모든 non-center 선을 same-role 교점에서 분할.
//   outer↔outer, mortar↔mortar, space↔space 교점에서 각각 틈(gap) 생성.
//   결과: 두 다중선이 교차하는 십자(+) 모양의 트림 완성.
//
void MultilineCCornerTool::doCrossJoin(LineItem *lineA, const QPointF &clickA,
                                        LineItem *lineB, const QPointF &clickB)
{
    if (!m_scene) return;

    const QString gidA = lineA->data(ML_DATA_GROUP_ID).toString();
    const QString gidB = lineB->data(ML_DATA_GROUP_ID).toString();

    // ── 그룹별 선 수집 (center 선 제외) ──────────────────────────────────────
    // 일반선(gid 없음): 클릭 위치에서 100px 이내 평행 일반선을 모두 수집해 가상 그룹 구성
    auto collectRegularGroup = [this](LineItem *ref, const QPointF &click)
        -> QList<LineItem *> {
        QList<LineItem *> group;
        group << ref;
        if (!m_scene) return group;
        double extTol = 100.0;
        if (!m_scene->views().isEmpty()) {
            double scale = m_scene->views().constFirst()->viewportTransform().m11();
            if (scale > 1e-6) extTol = 100.0 / scale;
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
            // 기준선 ↔ 후보선 선간 간격 (클릭 위치 무관 순수 기하 거리)
            const QPointF dlines  = q.p1() - refF.p1();
            const double  spacing = qAbs(dn.x() * dlines.y() - dn.y() * dlines.x());
            if (spacing > extTol) continue;
            // 클릭의 선방향 투영이 후보선 범위를 extTol 초과하면 제외
            // → 같은 방향이지만 다른 위치(구간)의 원거리 평행선 차단
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
                tr("C-Corner: 대상 선을 찾을 수 없습니다."), CommandDock::Info);
        return;
    }

    // bPerp: groupB 방향의 수직(좌/우 탐색용), aPerp: groupA 방향의 수직
    const QPointF bDir = (toLineF(lineB).p2() - toLineF(lineB).p1());
    const QPointF aDir = (toLineF(lineA).p2() - toLineF(lineA).p1());
    const QPointF bPerp(-bDir.y(), bDir.x());
    const QPointF aPerp(-aDir.y(), aDir.x());

    // ── Fast-path: 두 선이 모두 일반 단일선 ────────────────────────────────────
    // collectSplitOps 는 same-role 두 선이 필요해 단일선에선 동작 불가.
    // 단순히 교점까지 가장 가까운 끝점을 트림한다.
    if (groupA.size() == 1 && groupB.size() == 1) {
        IAppContext *ctx = getContext();
        if (!ctx || !ctx->undoStack()) return;
        const QLineF qa = toLineF(groupA[0]);
        const QLineF qb = toLineF(groupB[0]);
        QPointF P;
        if (qa.intersects(qb, &P) == QLineF::NoIntersection) {
            if (ctx->commandDock())
                ctx->commandDock()->logMessage(
                    tr("Corner: 교점을 찾을 수 없습니다."), CommandDock::Info);
            return;
        }
        const double dA1 = QLineF(qa.p1(), P).length(), dA2 = QLineF(qa.p2(), P).length();
        const double dB1 = QLineF(qb.p1(), P).length(), dB2 = QLineF(qb.p2(), P).length();
        const bool aAtP1 = dA1 < dA2;
        const bool bAtP1 = dB1 < dB2;
        ctx->undoStack()->beginMacro("Simple Corner");
        ctx->undoStack()->push(new ExtendEntityCommand(
            groupA[0], aAtP1 ? qa.p1() : qa.p2(), P, aAtP1));
        ctx->undoStack()->push(new ExtendEntityCommand(
            groupB[0], bAtP1 ? qb.p1() : qb.p2(), P, bAtP1));
        ctx->undoStack()->endMacro();
        if (ctx->commandDock())
            ctx->commandDock()->logMessage(tr("Corner 완료."), CommandDock::Info);
        return;
    }

    // 공선 세그먼트 중 클릭에 가장 가까운 세그먼트 선택 (도어 트림 잔여 제외).
    auto deduplicateByClick = [this](QList<LineItem *> &group,
                                     const QPointF &click,
                                     const QPointF &dir) {
        const double len = QLineF(QPointF(0, 0), dir).length();
        if (len < 1e-10) return;
        const QPointF nd = dir / len;
        const double  cp = QPointF::dotProduct(click, nd);
        auto projDist = [&](LineItem *li) -> double {
            const QLineF  q  = toLineF(li);
            const double  p1 = QPointF::dotProduct(q.p1(), nd);
            const double  p2 = QPointF::dotProduct(q.p2(), nd);
            const double  lo = qMin(p1, p2), hi = qMax(p1, p2);
            return (cp < lo) ? (lo - cp) : (cp > hi) ? (cp - hi) : 0.0;
        };
        auto isCollinear = [this](LineItem *a, LineItem *b) -> bool {
            const QLineF  qa   = toLineF(a);
            const double  lenA = qa.length();
            if (lenA < 1e-10) return false;
            const QPointF dnA  = (qa.p2() - qa.p1()) / lenA;
            const QLineF  qb   = toLineF(b);
            const QPointF dirB = qb.p2() - qb.p1();
            const double  lenB = qb.length();
            if (lenB < 1e-10) return false;
            if (qAbs(QPointF::dotProduct(dnA, dirB) / lenB) < 0.9999) return false;
            const QPointF d = qb.p1() - qa.p1();
            return qAbs(dnA.x() * d.y() - dnA.y() * d.x()) < 0.5;
        };
        QList<LineItem *> result;
        QList<bool>       used(group.size(), false);
        for (int i = 0; i < group.size(); ++i) {
            if (used[i]) continue;
            used[i] = true;
            LineItem *best  = group[i];
            double    bestD = projDist(group[i]);
            for (int j = i + 1; j < group.size(); ++j) {
                if (used[j] || !isCollinear(group[i], group[j])) continue;
                used[j] = true;
                const double d = projDist(group[j]);
                if (d < bestD) { bestD = d; best = group[j]; }
            }
            result << best;
        }
        if (!result.isEmpty()) group = result;
    };
    deduplicateByClick(groupA, clickA, aDir);
    deduplicateByClick(groupB, clickB, bDir);

    QList<SplitOp> splitOps;

    // groupA 각 선을 same-role groupB 두 선 교점에서 분할
    collectSplitOps(groupA, groupB, bPerp, splitOps);
    // groupB 각 선을 same-role groupA 두 선 교점에서 분할
    collectSplitOps(groupB, groupA, aPerp, splitOps);

    IAppContext *ctx = getContext();
    if (!ctx || !ctx->undoStack()) {
        for (int i = 0; i < splitOps.size(); ++i) {
            delete splitOps.at(i).newLine;
        }
        return;
    }
    if (splitOps.isEmpty()) {
        if (ctx->commandDock())
            ctx->commandDock()->logMessage(
                tr("C-Corner: 교점을 찾을 수 없습니다."), CommandDock::Info);
        return;
    }

    ctx->undoStack()->beginMacro("Multiline C-Corner");
    for (int i = 0; i < splitOps.size(); ++i) {
        const SplitOp &sop = splitOps.at(i);
        ctx->undoStack()->push(
            new ExtendEntityCommand(sop.origLine, sop.origP2, sop.cutNear, false));
        ctx->undoStack()->push(
            new AddEntityCommand(m_scene, sop.newLine));
    }
    ctx->undoStack()->endMacro();

    if (ctx->commandDock())
        ctx->commandDock()->logMessage(
            tr("C-Corner 완료: %1선 분할.").arg(splitOps.size()),
            CommandDock::Info);
}

// ── MultilineCCornerCommand ───────────────────────────────────────────────────
MultilineCCornerCommand::MultilineCCornerCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx)
{}

void MultilineCCornerCommand::execute()
{
    if (!m_ctx) return;
    m_ctx->startTool(new MultilineCCornerTool());
}
