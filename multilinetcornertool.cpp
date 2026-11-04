#include "multilinetcornertool.h"
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
//#include <algorithm>
#include <limits>

// ── ctor ─────────────────────────────────────────────────────────────────────
MultilineTCornerTool::MultilineTCornerTool(QObject *parent)
    : AbstractTool(parent)
{}

// ── activate / deactivate ────────────────────────────────────────────────────
void MultilineTCornerTool::activate(MyGraphicsScene *scene)
{
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    resetState();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("T-CORNER", CommandDock::Command);
    }
    showPrompt();
}

void MultilineTCornerTool::deactivate()
{
    resetState();
    AbstractTool::deactivate();
}

// ── helpers ───────────────────────────────────────────────────────────────────
void MultilineTCornerTool::resetState()
{
    m_crossbar = nullptr;
}

void MultilineTCornerTool::showPrompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    if (!m_crossbar) {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 교차선(통과하는 선)을 클릭 "), {});
    } else {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 줄기선(끝을 자를 선)의 자를 끝 근처를 클릭 "), {});
    }
}

QLineF MultilineTCornerTool::toLineF(LineItem *li) const
{
    MyLine d = li->getLine();
    return QLineF(d.start.toPointF(), d.end.toPointF());
}

// ── 보조 탐색 ─────────────────────────────────────────────────────────────────
//   itemAtPos() 가 빈 공간을 클릭했을 때 (2-Line 선 간격 사이 등)
//   씬 전체에서 ML_DATA_GROUP_ID 가 설정된 LineItem 중 가장 가까운 것을 반환.
LineItem *MultilineTCornerTool::findNearestMultilineItem(const QPointF &pos) const
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
        if (li->data(ML_DATA_GROUP_ID).toString().isEmpty()) continue; // ML 태그 선만

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
void MultilineTCornerTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene) return;
    IAppContext *ctx = getContext();

    if (button == Qt::RightButton) {
        onCancel();
        return;
    }
    if (button != Qt::LeftButton) return;

    // 1단계: CadPicker 기반 정밀 히트 판정
    QGraphicsItem *hit = itemAtPos(pos);
    LineItem *li = dynamic_cast<LineItem *>(hit);

    // 2단계: 히트 실패 시에만 ML 태그 선 확장 탐색 수행
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
        // ── 1차: 교차선 선택 ─────────────────────────────────────────────────
        m_crossbar      = li;
        m_crossbarClick = pos;
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(
                gid.isEmpty() ? tr("교차선 선택: 일반 선")
                              : tr("교차선 선택: 그룹 [%1...]").arg(gid.left(8)),
                CommandDock::Info);
        showPrompt();
    } else {
        // ── 2차: 줄기선 선택 ─────────────────────────────────────────────────
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

void MultilineTCornerTool::mouseMove(QPointF /*pos*/)  {}
void MultilineTCornerTool::mouseRelease(QPointF /*pos*/, Qt::MouseButton /*button*/) {}

bool MultilineTCornerTool::processCommand(const QString & /*command*/)
{
    return false;
}

// ── onFinish / onCancel ───────────────────────────────────────────────────────
void MultilineTCornerTool::onFinish()
{
    resetState();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->setPrompt(" Command : ");
    emit toolFinished();
}

void MultilineTCornerTool::onCancel()
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
//
// T-코너 처리 원리 (role 기반 매칭):
//   [A] 줄기 각 선 → 같은 role 의 near-face 교차선과의 교점까지 트림.
//   [B] near-face 교차선 절반 → 같은 role 의 줄기 좌/우 두 선 교점에서 분할.
//   outer↔outer, mortar↔mortar, space↔space 로 연결되어 계층별 틈 생성.
//
void MultilineTCornerTool::doTJoin(LineItem *crossbar, const QPointF &clickA,
                                    LineItem *stem,    const QPointF &clickB)
{
    if (!m_scene) return;

    const QString gidA = crossbar->data(ML_DATA_GROUP_ID).toString();
    const QString gidB = stem->data(ML_DATA_GROUP_ID).toString();

    // ── 그룹별 선 수집 (center 선 제외) ──────────────────────────────────────
    // 일반선(gid 없음): 클릭 위치에서 100px 이내 평행 일반선을 모두 수집해 가상 그룹 구성
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
        if (totalWidth < 1e-5) totalWidth = 100.0;
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

    if (groupA.isEmpty() || groupB.isEmpty()) {
        IAppContext *ctx = getContext();
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(
                tr("T-Corner: 대상 선을 찾을 수 없습니다."), CommandDock::Info);
        return;
    }

    // ── 방향 벡터, 클릭 근처 세그먼트 필터, 트림 끝 결정 ────────────────────────
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
    // groupA(교차선): 공선 세그먼트를 제거하지 않는다.
    // 교차선이 줄기 양쪽으로 분리되어 있는 경우 모든 세그먼트가 필요.
    deduplicateByClick(groupB, clickB, bDir);

    // ── 방향 필터: groupA/B에서 방향 불일치 세그먼트 제거 ────────────────────────
    // 다중선을 꺾인 경로(L/U자 등)로 생성하면 가로·세로 세그먼트가 같은 UUID를 공유.
    // 줄기 방향(bDir)과 평행한 선이 groupA에 섞이면 avgProj 계산이 왜곡되어
    // 실제 교차면이 near-face로 선택되지 않는다.
    // 해결: bDir과 평행(|dot|≥0.9)한 선은 groupA에서, 수직(|dot|<0.9)한 선은 groupB에서 제거.
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

            applyDirFilter(groupA, false); // 교차선: 줄기와 수직(비평행)인 선만 유지
            applyDirFilter(groupB, true);  // 줄기선: 줄기 방향과 평행한 선만 유지
        }
    }
    if (groupA.isEmpty() || groupB.isEmpty()) {
        IAppContext *ctx3 = getContext();
        if (ctx3 && ctx3->commandDock())
            ctx3->commandDock()->logMessage(
                tr("T-Corner: 방향 필터 후 대상 선 없음."), CommandDock::Info);
        return;
    }

    const bool    trimBHighEnd =
        QPointF::dotProduct(clickB - (refBF.p1() + refBF.p2()) / 2.0, bDir) > 0;

    // approachVec: trimEnd→nonTrimEnd 방향(줄기 몸체가 향하는 방향)
    const QPointF approachVec = trimBHighEnd
                                    ? QPointF(-bDir.x(), -bDir.y())
                                    : bDir;

    // bPerp: 줄기의 수직 방향(좌/우 구분용)
    const QPointF bPerp(-bDir.y(), bDir.x());

    // ── 일반선에 다중선과 같은 가상 역할(Role) 부여 ─────────────────────────────
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
            if (!li->data(ML_DATA_ROLE).toString().isEmpty()) continue;
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

    // 교차선 중점의 approachVec 투영 평균 → near(줄기 접근 면) / far(반대 면) 구분
    // 단일 교차선(일반선 포함)은 항상 near-face 로 취급(-inf 사용)
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

    // ── [A] 줄기 트림 ops ──────────────────────────────────────────────────────
    // 각 줄기 선 → 같은 role 의 near-face 교차선 교점까지 트림.
    // same-role 교차선이 없으면 near-face 중 nonTrimEnd 거리 최소 선으로 fallback.
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

        QPointF bestP;
        bool    found   = false;
        double  minDist = std::numeric_limits<double>::max();

        // 1차: same-role near-face 교차선
        for (int j = 0; j < groupA.size(); ++j) {
            LineItem *cl = groupA.at(j);
            if (!isNearFace(cl)) continue;
            if (cl->data(ML_DATA_ROLE).toString() != stemRole) continue;
            QPointF P;
            if (!getIntersection(qb, toLineF(cl), &P)) continue;
            const QLineF clF = toLineF(cl);
            const double lenCl = clF.length();
            if (lenCl > 1e-9) {
                const double tCl = QPointF::dotProduct(P - clF.p1(), clF.p2() - clF.p1()) / (lenCl * lenCl);
                if (tCl < -0.15 || tCl > 1.15) continue;
            }
            const double d = QLineF(nonTrimEnd, P).length();
            if (d < minDist) { minDist = d; bestP = P; found = true; }
        }
        // fallback: near-face 중 거리 최소
        if (!found) {
            if (stemRole == "space") continue;

            for (int j = 0; j < groupA.size(); ++j) {
                LineItem *cl = groupA.at(j);
                if (!isNearFace(cl)) continue;
                QPointF P;
                if (!getIntersection(qb, toLineF(cl), &P)) continue;
                const QLineF clF = toLineF(cl);
                const double lenCl = clF.length();
                if (lenCl > 1e-9) {
                    const double tCl = QPointF::dotProduct(P - clF.p1(), clF.p2() - clF.p1()) / (lenCl * lenCl);
                    if (tCl < -0.15 || tCl > 1.15) continue;
                }
                const double d = QLineF(nonTrimEnd, P).length();
                if (d < minDist) { minDist = d; bestP = P; found = true; }
            }
        }
        if (found)
            trimOps << TrimOp{ stemLine, trimEnd, bestP, bAtP1 };
    }

    // ── [B] 교차선 분할 ops ───────────────────────────────────────────────────
    // near-face 교차선(count/2)만 분할.
    // 각 교차선 선 → 같은 role 의 줄기 좌/우 두 선 교점에서 분할.
    // same-role 줄기 선이 없으면 최외곽 줄기 선으로 fallback.
    // origPos: 이동되는 끝점 원래 위치, isFirstEnd: true=p1, false=p2
    // newLine: nullptr=트림만, 비nullptr=분할 후 추가 세그먼트
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

        // 교차선 역할이 "space" 인데 줄기선에 "space" 가 없다면 분할(트림)을 건너뜀
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

        // same-role 줄기 선 중 bPerp 최소(sameL)/최대(sameR) 탐색
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
        // fallback: 최외곽 줄기 선
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
        const QPointF ptMin = (tL <= tR) ? P_L : P_R; // t 작은 쪽 교점
        const QPointF ptMax = (tL <= tR) ? P_R : P_L; // t 큰 쪽 교점

        if (tMin >= -eps && tMax <= 1.0 + eps) {
            // Case 1: 양쪽 모두 선분 내 → 분할 (교차선 하나가 줄기 폭 전체를 포함)
            if (QLineF(P_L, P_R).length() < 1e-6) continue;
            LineItem *nl = new LineItem(MyVector(ptMax), MyVector(qla.p2()));
            nl->setPen(crossLine->pen());
            nl->setLayerName(crossLine->layerName());
            nl->setData(ML_DATA_GROUP_ID, crossLine->data(ML_DATA_GROUP_ID));
            nl->setData(ML_DATA_ROLE,     crossLine->data(ML_DATA_ROLE));
            splitOps << SplitOp{ crossLine, qla.p2(), ptMin, false, nl };
        } else if (tMin >= -eps && tMin <= 1.0 + eps) {
            // Case 2: tMin만 범위 내, tMax > 1 → p2를 ptMin으로 트림
            // (줄기 왼쪽/시작 쪽 세그먼트)
            splitOps << SplitOp{ crossLine, qla.p2(), ptMin, false, nullptr };
        } else if (tMax <= 1.0 + eps && tMax >= -eps) {
            // Case 3: tMax만 범위 내, tMin < 0 → p1을 ptMax으로 트림
            // (줄기 오른쪽/끝 쪽 세그먼트)
            splitOps << SplitOp{ crossLine, qla.p1(), ptMax, true, nullptr };
        }
        // Case 4: 양쪽 모두 범위 밖 → skip (선분이 줄기 폭 내에 완전히 포함)
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
                tr("T-Corner: 교점을 찾을 수 없습니다."), CommandDock::Info);
        return;
    }

    ctx->undoStack()->beginMacro("Multiline T-Corner");

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
            tr("T-Corner 완료: 줄기 %1선 트림 / 교차선 %2선 분할.")
                .arg(trimOps.size()).arg(splitOps.size()),
            CommandDock::Info);
}

// ── MultilineTCornerCommand ───────────────────────────────────────────────────
MultilineTCornerCommand::MultilineTCornerCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx)
{}

void MultilineTCornerCommand::execute()
{
    if (!m_ctx) return;
    m_ctx->startTool(new MultilineTCornerTool());
}
