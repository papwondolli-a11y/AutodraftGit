#include "multilineraycastcornertool.h"
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
MultilineRaycastTCornerTool::MultilineRaycastTCornerTool(QObject *parent)
    : AbstractTool(parent)
{}

// ── activate / deactivate ────────────────────────────────────────────────────
void MultilineRaycastTCornerTool::activate(MyGraphicsScene *scene)
{
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    resetState();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("RAYCAST T-CORNER ACTIVE", CommandDock::Command);
    }
    showPrompt();
}

void MultilineRaycastTCornerTool::deactivate()
{
    resetState();
    AbstractTool::deactivate();
}

// ── helpers ───────────────────────────────────────────────────────────────────
void MultilineRaycastTCornerTool::resetState()
{
    m_crossbar = nullptr;
}

void MultilineRaycastTCornerTool::showPrompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    if (!m_crossbar) {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 트림 기준선  클릭 "), {});
    } else {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 트림 될 선  클릭 "), {});
    }
}

QLineF MultilineRaycastTCornerTool::toLineF(LineItem *li) const
{
    MyLine d = li->getLine();
    return QLineF(d.start.toPointF(), d.end.toPointF());
}

// ── 보조 탐색 ─────────────────────────────────────────────────────────────────
LineItem *MultilineRaycastTCornerTool::findNearestMultilineItem(const QPointF &pos) const
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
void MultilineRaycastTCornerTool::mousePress(QPointF pos, Qt::MouseButton button)
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

    if (!li) {
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

    if (!m_crossbar) {
        m_crossbar      = li;
        m_crossbarClick = pos;
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(
                gid.isEmpty() ? tr("교차선 선택: 일반 선")
                              : tr("교차선 선택: 그룹 [%1...]").arg(gid.left(8)),
                CommandDock::Info);
        showPrompt();
    } else {
        QString gidA = m_crossbar->data(ML_DATA_GROUP_ID).toString();
        if (!gid.isEmpty() && !gidA.isEmpty() && gid == gidA) {
            if (ctx && ctx->commandDock())
                ctx->commandDock()->logMessage(
                    tr("같은 그룹입니다. 다른 선을 선택하세요."),
                    CommandDock::Info);
            return;
        }

        doTJoin(m_crossbar, m_crossbarClick, li, pos);
        resetState();
        showPrompt();
    }
}

void MultilineRaycastTCornerTool::mouseMove(QPointF /*pos*/)  {}
void MultilineRaycastTCornerTool::mouseRelease(QPointF /*pos*/, Qt::MouseButton /*button*/) {}

bool MultilineRaycastTCornerTool::processCommand(const QString & /*command*/)
{
    return false;
}

// ── onFinish / onCancel ───────────────────────────────────────────────────────
void MultilineRaycastTCornerTool::onFinish()
{
    resetState();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->setPrompt(" Command : ");
    emit toolFinished();
}

void MultilineRaycastTCornerTool::onCancel()
{
    if (m_crossbar) {
        resetState();
        showPrompt();
        IAppContext *ctx = getContext();
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(tr("선택 취소. 다시 클릭하세요."), CommandDock::Info);
    } else {
        onFinish();
    }
}

// ── doTJoin ───────────────────────────────────────────────────────────────────
void MultilineRaycastTCornerTool::doTJoin(LineItem *crossbar, const QPointF &clickA,
                                          LineItem *stem,    const QPointF &clickB)
{
    if (!m_scene) return;

    const QString gidA = crossbar->data(ML_DATA_GROUP_ID).toString();
    const QString gidB = stem->data(ML_DATA_GROUP_ID).toString();

    // ── 물리적 연장 거리 계산 헬퍼 람다 ──
    auto getExtLen = [](const QPointF &pt, const QLineF &seg) -> double {
        const double l = seg.length();
        if (l < 1e-9) return QLineF(seg.p1(), pt).length();
        const double t = QPointF::dotProduct(pt - seg.p1(), seg.p2() - seg.p1()) / (l * l);
        if (t >= 0.0 && t <= 1.0) return 0.0;
        else if (t < 0.0) return qAbs(t) * l;
        else return (t - 1.0) * l;
    };

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
            if (qAbs(QPointF::dotProduct(dn, dn2)) < 0.99) continue;
            
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
    if (gidA.isEmpty()) groupA = collectRegularGroup(crossbar, clickA);
    if (gidB.isEmpty()) groupB = collectRegularGroup(stem,     clickB);
    for (QGraphicsItem *item : m_scene->items()) {
        auto *li = dynamic_cast<LineItem *>(item);
        if (!li) continue;
        const QString gid  = li->data(ML_DATA_GROUP_ID).toString();
        const QString role = li->data(ML_DATA_ROLE).toString();
        if (role == "center") continue;
        if (!gid.isEmpty() && gid == gidA)      groupA << li;
        else if (!gid.isEmpty() && gid == gidB) groupB << li;
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

    applyDistanceFilter(groupA, crossbar);
    applyDistanceFilter(groupB, stem);

    // ── 일반선 주변 객체 간섭 방지: 수직 거리 기준 가장 가까운 2개만 남김 ─────────
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

    if (gidA.isEmpty()) {
        std::sort(groupA.begin(), groupA.end(), [&](LineItem *a, LineItem *b) {
            return perpDist(a, clickA) < perpDist(b, clickA);
        });
        if (groupA.size() > 2) {
            groupA = groupA.mid(0, 2);
        }
    }
    if (gidB.isEmpty()) {
        std::sort(groupB.begin(), groupB.end(), [&](LineItem *a, LineItem *b) {
            return perpDist(a, clickB) < perpDist(b, clickB);
        });
        if (groupB.size() > 2) {
            groupB = groupB.mid(0, 2);
        }
    }

    if (groupA.isEmpty() || groupB.isEmpty()) {
        IAppContext *ctx3 = getContext();
        if (ctx3 && ctx3->commandDock())
            ctx3->commandDock()->logMessage(
                tr("T-Corner: 대상 선 없음."), CommandDock::Info);
        return;
    }

    const QLineF  refBF = toLineF(stem);
    const QPointF bDir  = refBF.p2() - refBF.p1();

    auto getIntersection = [](const QLineF &l1, const QLineF &l2, QPointF *rP) -> bool {
        const double denom = (l2.y2() - l2.y1()) * (l1.x2() - l1.x1()) - (l2.x2() - l2.x1()) * (l1.y2() - l1.y1());
        if (qAbs(denom) < 1e-9) return false;
        const double num = (l2.x2() - l2.x1()) * (l1.y1() - l2.y1()) - (l2.y2() - l2.y1()) * (l1.x1() - l2.x1());
        const double t = num / denom;
        if (rP) *rP = QPointF(l1.x1() + t * (l1.x2() - l1.x1()), l1.y1() + t * (l1.y2() - l1.y1()));
        return true;
    };

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
    deduplicateByClick(groupB, clickB, bDir);

    {
        const double bFilterLen = QLineF(QPointF(0, 0), bDir).length();
        if (bFilterLen > 1e-10) {
            const QPointF bDn = bDir / bFilterLen;
            constexpr double parallelTh = 0.9;

            auto applyDirFilter = [&](QList<LineItem *> &grp, bool keepParallel) {
                QList<LineItem *> out;
                for (int k = 0; k < grp.size(); ++k) {
                    LineItem *li = grp.at(k);
                    const QLineF  q   = toLineF(li);
                    const double  l   = q.length();
                    if (l < 1e-10) continue;
                    const double  dot = qAbs(QPointF::dotProduct(bDn, (q.p2() - q.p1()) / l));
                    if (keepParallel ? (dot >= parallelTh) : (dot < parallelTh))
                        out << li;
                }
                if (!out.isEmpty()) grp = out;
            };

            applyDirFilter(groupA, false);
            applyDirFilter(groupB, true);
        }
    }
    if (groupA.isEmpty() || groupB.isEmpty()) {
        IAppContext *ctx3 = getContext();
        if (ctx3 && ctx3->commandDock())
            ctx3->commandDock()->logMessage(
                tr("Raycast T-Corner: 방향 필터 후 대상 선 없음."), CommandDock::Info);
        return;
    }

    QPointF P_int;
    if (!getIntersection(refBF, toLineF(crossbar), &P_int)) {
        P_int = (refBF.p1() + refBF.p2()) / 2.0;
    }

    const bool    trimBHighEnd =
        QPointF::dotProduct(clickB - P_int, bDir) > 0;

    const QPointF approachVec = trimBHighEnd
                                    ? QPointF(-bDir.x(), -bDir.y())
                                    : bDir;

    const QPointF bPerp(-bDir.y(), bDir.x());

    // ── 일반선 가상 역할 부여 ───────────────────────────────────────────────────
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
    assignVirtualRoles(groupA, approachVec);
    assignVirtualRoles(groupB, bPerp);

    // ── near-face 판정 람다 ────────────────────────────────────────────────────
    auto isNearFace = [&](LineItem *cl) -> bool {
        const QString role = cl->data(ML_DATA_ROLE).toString();
        const QLineF clF = toLineF(cl);
        const QPointF m = (clF.p1() + clF.p2()) / 2.0;
        const double proj = QPointF::dotProduct(m, approachVec);

        bool hasFurtherNear = false;
        for (int k = 0; k < groupA.size(); ++k) {
            LineItem *other = groupA.at(k);
            if (other == cl) continue;
            if (other->data(ML_DATA_ROLE).toString() != role) continue;

            const QLineF otherF = toLineF(other);
            const QPointF otherM = (otherF.p1() + otherF.p2()) / 2.0;

            const QPointF d = otherM - m;
            const QPointF clDir = clF.p2() - clF.p1();
            const double len = QLineF(QPointF(0, 0), clDir).length();
            if (len < 1e-10) continue;
            const QPointF dn = clDir / len;
            const double perpDist = qAbs(dn.x() * d.y() - dn.y() * d.x());

            if (perpDist < 5.0) continue;

            const double otherProj = QPointF::dotProduct(otherM, approachVec);
            if (otherProj > proj + 1e-3) {
                hasFurtherNear = true;
                break;
            }
        }
        return !hasFurtherNear;
    };

    // ── [A] 줄기 트림 ops (광선 발사 방식, Raycasting) ───────────────────────
    struct TrimOp {
        LineItem *line;
        QPointF   oldPos;
        QPointF   newPos;
        bool      isFirstEnd;
    };
    QList<TrimOp> trimOps;

    for (int i = 0; i < groupB.size(); ++i) {
        LineItem *stemLine = groupB.at(i);
        const QString stemRole   = stemLine->data(ML_DATA_ROLE).toString();
        const QLineF  qb         = toLineF(stemLine);
        const double  pB1        = QPointF::dotProduct(qb.p1(), bDir);
        const double  pB2        = QPointF::dotProduct(qb.p2(), bDir);
        const bool    bAtP1      = trimBHighEnd ? (pB1 > pB2) : (pB1 < pB2);
        const QPointF trimEnd    = bAtP1 ? qb.p1() : qb.p2();
        const QPointF nonTrimEnd = bAtP1 ? qb.p2() : qb.p1();

        // 광선 방향 설정 (몸체 -> 자를 끝점 방향으로 전진)
        const QPointF rayDir = trimEnd - nonTrimEnd;
        const double rayLen = QLineF(QPointF(0,0), rayDir).length();
        if (rayLen < 1e-10) continue;
        const QPointF rayDn = rayDir / rayLen;

        QPointF bestP;
        bool    found   = false;
        double  minDist = std::numeric_limits<double>::max();

        // 1차: Same-role near-face 광선 교차선 탐색
        for (int j = 0; j < groupA.size(); ++j) {
            LineItem *cl = groupA.at(j);
            if (!isNearFace(cl)) continue;
            if (cl->data(ML_DATA_ROLE).toString() != stemRole) continue;

            QPointF P;
            if (!getIntersection(qb, toLineF(cl), &P)) continue;

            // 광선 진행 방향(전방) 확인 (t >= -10.0 미세 마진)
            const double tRay = QPointF::dotProduct(P - nonTrimEnd, rayDn);
            if (tRay < -10.0) continue;

            const QLineF clF = toLineF(cl);
            // [물리적 연장 길이 필터] 교차 연장 한계를 800px로 상향 및 줄기선 연장 길이 판단으로 수정
            if (getExtLen(P, qb) > 800.0) continue;
            if (getExtLen(P, clF) > 800.0) continue;

            const double d = QLineF(trimEnd, P).length();
            if (d < minDist) { minDist = d; bestP = P; found = true; }
        }

        // 2차: fallback (공간선 제외한 경우, 임의의 near-face 와의 광선 교차)
        if (!found) {
            if (stemRole == "space") continue; // 공간선은 동일 역할 없으면 통과

            for (int j = 0; j < groupA.size(); ++j) {
                LineItem *cl = groupA.at(j);
                if (!isNearFace(cl)) continue;

                QPointF P;
                if (!getIntersection(qb, toLineF(cl), &P)) continue;

                const double tRay = QPointF::dotProduct(P - nonTrimEnd, rayDn);
                if (tRay < -10.0) continue;

                const QLineF clF = toLineF(cl);
                // [물리적 연장 길이 필터] 교차 연장 한계를 800px로 상향 및 줄기선 연장 길이 판단으로 수정
                if (getExtLen(P, qb) > 800.0) continue;
                if (getExtLen(P, clF) > 800.0) continue;

                const double d = QLineF(trimEnd, P).length();
                if (d < minDist) { minDist = d; bestP = P; found = true; }
            }
        }

        if (found)
            trimOps << TrimOp{ stemLine, trimEnd, bestP, bAtP1 };
    }

    // ── [B] 교차선 분할 ops ───────────────────────────────────────────────────
    struct SplitOp {
        LineItem *origLine;
        QPointF   origPos;
        QPointF   newPos;
        bool      isFirstEnd;
        LineItem *newLine;
    };
    QList<SplitOp> splitOps;

    for (int i = 0; i < groupA.size(); ++i) {
        LineItem *crossLine = groupA.at(i);
        if (!isNearFace(crossLine)) continue;

        const QString crossRole = crossLine->data(ML_DATA_ROLE).toString();

        if (crossRole == "space") {
            bool hasSpaceInStem = false;
            for (int j = 0; j < groupB.size(); ++j) {
                if (groupB.at(j)->data(ML_DATA_ROLE).toString() == "space") {
                    hasSpaceInStem = true;
                    break;
                }
            }
            if (!hasSpaceInStem) continue;
        }

        const QLineF  qla       = toLineF(crossLine);

        LineItem *sameL = nullptr, *sameR = nullptr;
        double    mnP   =  std::numeric_limits<double>::max();
        double    mxP   = -std::numeric_limits<double>::max();
        for (int j = 0; j < groupB.size(); ++j) {
            LineItem *sl = groupB.at(j);
            if (sl->data(ML_DATA_ROLE).toString() != crossRole) continue;
            const QPointF sm  = (toLineF(sl).p1() + toLineF(sl).p2()) / 2.0;
            const double  prj = QPointF::dotProduct(sm, bPerp);
            if (prj < mnP) { mnP = prj; sameL = sl; }
            if (prj > mxP) { mxP = prj; sameR = sl; }
        }
        if (!sameL || !sameR || sameL == sameR) {
            sameL = nullptr; sameR = nullptr;
            mnP =  std::numeric_limits<double>::max();
            mxP = -std::numeric_limits<double>::max();
            for (int j = 0; j < groupB.size(); ++j) {
                LineItem *sl = groupB.at(j);
                const QPointF sm  = (toLineF(sl).p1() + toLineF(sl).p2()) / 2.0;
                const double  prj = QPointF::dotProduct(sm, bPerp);
                if (prj < mnP) { mnP = prj; sameL = sl; }
                if (prj > mxP) { mxP = prj; sameR = sl; }
            }
        }
        if (!sameL || !sameR || sameL == sameR) continue;

        QPointF P_L, P_R;
        if (!getIntersection(qla, toLineF(sameL), &P_L)) continue;
        if (!getIntersection(qla, toLineF(sameR), &P_R)) continue;

        const QPointF aDirSeg = qla.p2() - qla.p1();
        const double  lenSq   = QPointF::dotProduct(aDirSeg, aDirSeg);
        if (lenSq < 1e-10) continue;

        const double tL = QPointF::dotProduct(P_L - qla.p1(), aDirSeg) / lenSq;
        const double tR = QPointF::dotProduct(P_R - qla.p1(), aDirSeg) / lenSq;

        constexpr double eps = 1e-2;
        const double  tMin  = qMin(tL, tR);
        const double  tMax  = qMax(tL, tR);
        const QPointF ptMin = (tL <= tR) ? P_L : P_R;
        const QPointF ptMax = (tL <= tR) ? P_R : P_L;

        if (tMin >= -eps && tMax <= 1.0 + eps) {
            if (QLineF(P_L, P_R).length() < 1e-6) continue;
            LineItem *nl = new LineItem(MyVector(ptMax), MyVector(qla.p2()));
            nl->setPen(crossLine->pen());
            nl->setLayerName(crossLine->layerName());
            nl->setData(ML_DATA_GROUP_ID, crossLine->data(ML_DATA_GROUP_ID));
            nl->setData(ML_DATA_ROLE,     crossLine->data(ML_DATA_ROLE));
            splitOps << SplitOp{ crossLine, qla.p2(), ptMin, false, nl };
        } else if (tMin >= -eps && tMin <= 1.0 + eps) {
            splitOps << SplitOp{ crossLine, qla.p2(), ptMin, false, nullptr };
        } else if (tMax <= 1.0 + eps && tMax >= -eps) {
            splitOps << SplitOp{ crossLine, qla.p1(), ptMax, true, nullptr };
        }
    }

    // ── Undo/Redo 적용 ────────────────────────────────────────────────────────
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->undoStack()) {
        for (int i = 0; i < splitOps.size(); ++i) {
            const SplitOp &sop = splitOps.at(i);
            delete sop.newLine;
        }
        return;
    }
    if (trimOps.isEmpty() && splitOps.isEmpty()) {
        if (ctx->commandDock())
            ctx->commandDock()->logMessage(
                tr("Raycast T-Corner: 교점을 찾을 수 없습니다."), CommandDock::Info);
        return;
    }

    ctx->undoStack()->beginMacro("Multiline Raycast T-Corner");

    for (int i = 0; i < trimOps.size(); ++i) {
        const TrimOp &op = trimOps.at(i);
        ctx->undoStack()->push(
            new ExtendEntityCommand(op.line, op.oldPos, op.newPos, op.isFirstEnd));
    }

    for (int i = 0; i < splitOps.size(); ++i) {
        const SplitOp &sop = splitOps.at(i);
        ctx->undoStack()->push(
            new ExtendEntityCommand(sop.origLine, sop.origPos, sop.newPos, sop.isFirstEnd));
        if (sop.newLine)
            ctx->undoStack()->push(new AddEntityCommand(m_scene, sop.newLine));
    }

    ctx->undoStack()->endMacro();

    if (ctx->commandDock())
        ctx->commandDock()->logMessage(
            tr("Raycast T-Corner 완료: 줄기 %1선 트림 / 교차선 %2선 분할.")
                .arg(trimOps.size()).arg(splitOps.size()),
            CommandDock::Info);
}

// ── MultilineRaycastTCornerCommand ───────────────────────────────────────────
MultilineRaycastTCornerCommand::MultilineRaycastTCornerCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx)
{}

void MultilineRaycastTCornerCommand::execute()
{
    if (!m_ctx) return;
    m_ctx->startTool(new MultilineRaycastTCornerTool());
}
