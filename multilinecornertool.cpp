#include "multilinecornertool.h"
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

// ── ctor ─────────────────────────────────────────────────────────────────────
MultilineCornerTool::MultilineCornerTool(QObject *parent)
    : AbstractTool(parent)
{}

// ── activate / deactivate ────────────────────────────────────────────────────
void MultilineCornerTool::activate(MyGraphicsScene *scene)
{
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    resetState();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("L-CORNER", CommandDock::Command);
    }
    showPrompt();
}

void MultilineCornerTool::deactivate()
{
    resetState();
    AbstractTool::deactivate();
}

// ── helpers ───────────────────────────────────────────────────────────────────
void MultilineCornerTool::resetState()
{
    m_firstLine = nullptr;
}

void MultilineCornerTool::showPrompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    if (!m_firstLine) {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 첫 번째 선을 코너 근처에서 클릭 "), {});
    } else {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 두 번째 선을 코너 근처에서 클릭 "), {});
    }
}

QLineF MultilineCornerTool::toLineF(LineItem *li) const
{
    MyLine d = li->getLine();
    return QLineF(d.start.toPointF(), d.end.toPointF());
}

// ── helpers ───────────────────────────────────────────────────────────────────
//
// 다중선 전용 확장 탐색
//   - 1단계: itemAtPos() 로 정확한 히트 시도
//   - 2단계: 실패하거나 gid 가 없으면 씬 전체에서 ML_DATA_GROUP_ID 가 설정된
//            LineItem 중 뷰 기준 100px 이내에서 가장 가까운 것을 반환
//   → 2-Line / 4-Line 에서 선 사이 빈 공간을 클릭해도 인식되도록 한다.
LineItem *MultilineCornerTool::findNearestMultilineItem(const QPointF &pos) const
{
    if (!m_scene) return nullptr;

    // 뷰 스케일 보정 — 100 screen-px → world 단위 변환
    // (2-Line 선 간격이 100px 이하일 때 가운데 클릭도 인식되도록 넉넉히 설정)
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

        // 무한 연장선 기준 수직 거리
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
void MultilineCornerTool::mousePress(QPointF pos, Qt::MouseButton button)
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

    // 2단계: 히트 실패 또는 ML 태그 없는 선 → ML 태그 선 확장 탐색
    // (Pick 모드 원본 중심선이 잡혔을 때도 ML 외곽선으로 교체)
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
        // ── 1차 선택 ────────────────────────────────────────────────────────
        m_firstLine  = li;
        m_firstClick = pos;
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(
                gid.isEmpty() ? tr("1선택: 일반 선")
                              : tr("1선택: 그룹 [%1...]").arg(gid.left(8)),
                CommandDock::Info);
        showPrompt();
    } else {
        // ── 2차 선택 ────────────────────────────────────────────────────────
        QString gidA = m_firstLine->data(ML_DATA_GROUP_ID).toString();
        if (!gid.isEmpty() && !gidA.isEmpty() && gid == gidA) {
            if (ctx && ctx->commandDock())
                ctx->commandDock()->logMessage(
                    tr("같은 그룹입니다. 다른 선을 선택하세요."),
                    CommandDock::Info);
            return;
        }

        doCornerJoin(m_firstLine, m_firstClick, li, pos);
        // 코너 작업 후 반복 모드 (1차 선택 초기화)
        resetState();
        showPrompt();
    }
}

void MultilineCornerTool::mouseMove(QPointF /*pos*/)  {}
void MultilineCornerTool::mouseRelease(QPointF /*pos*/, Qt::MouseButton /*button*/) {}

bool MultilineCornerTool::processCommand(const QString & /*command*/)
{
    return false;
}

// ── onFinish / onCancel ───────────────────────────────────────────────────────
void MultilineCornerTool::onFinish()
{
    resetState();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->setPrompt(" Command : ");
    emit toolFinished();
}

void MultilineCornerTool::onCancel()
{
    if (m_firstLine) {
        // 1차 선택만 취소, 툴은 유지
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
//
// 핵심 매칭 원리:
//   클릭 위치로부터 수직 거리가 가까운 선끼리 같은 순위로 짝지음.
//   - groupA 정렬: clickA 에서 수직 거리 오름차순
//   - groupB 정렬: clickB 에서 수직 거리 오름차순
//   - 같은 인덱스 → 같은 "깊이"의 선 → outer↔outer, inner↔inner 자동 매칭
//
// 예: 수직 4-Line vs 수평 4-Line, 좌하단 클릭
//   A(수평) 오름차순: bot-outer(가장 가까움) → bot-mortar → top-mortar → top-outer
//   B(수직) 오름차순: left-outer(가장 가까움) → left-mortar → right-mortar → right-outer
//   → bot-outer ↔ left-outer  (내측 코너)
//   → top-outer ↔ right-outer (외측 코너)  ← 올바른 L-코너 형태
//
void MultilineCornerTool::doCornerJoin(LineItem *lineA, const QPointF &clickA,
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
                tr("L-Corner: 대상 선을 찾을 수 없습니다."), CommandDock::Info);
        return;
    }

    // ── 방향 벡터 (클릭된 기준선) ────────────────────────────────────────────
    const QLineF  refAF = toLineF(lineA);
    const QLineF  refBF = toLineF(lineB);
    const QPointF aDir  = refAF.p2() - refAF.p1();
    const QPointF bDir  = refBF.p2() - refBF.p1();

    // 클릭한 쪽을 트림, 반대쪽(먼 쪽)을 유지
    // dot > 0 → 클릭이 p2(HIGH) 쪽 → 트림 HIGH 끝
    // dot < 0 → 클릭이 p1(LOW)  쪽 → 트림 LOW 끝 → trimHighEnd = false
    const bool trimAHighEnd =
        QPointF::dotProduct(clickA - (refAF.p1() + refAF.p2()) / 2.0, aDir) > 0;
    const bool trimBHighEnd =
        QPointF::dotProduct(clickB - (refBF.p1() + refBF.p2()) / 2.0, bDir) > 0;

    // ── 클릭 위치에서 선까지의 수직 거리 (무한 연장선 기준) ─────────────────
    // perpDist = |cross(dir_norm, click - p1)| = 선에 수직 거리
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

    // ── 두 그룹을 "클릭에서 수직 거리" 오름차순 정렬 ─────────────────────────
    std::sort(groupA.begin(), groupA.end(),
              [&](LineItem *a, LineItem *b) {
                  return perpDist(a, clickA) < perpDist(b, clickA);
              });
    std::sort(groupB.begin(), groupB.end(),
              [&](LineItem *a, LineItem *b) {
                  return perpDist(a, clickB) < perpDist(b, clickB);
              });

    // 공선 세그먼트 중 클릭에 가장 가까운 세그먼트 선택.
    // 고정 허용값 대신 클릭 투영 거리(projDist)를 사용해 짧은 세그먼트에서도 안전.
    // 거리가 같으면 trimHighEnd 방향으로 가장 멀리 뻗은 세그먼트를 보조 기준으로 사용.
    auto deduplicateByClick = [this](QList<LineItem *> &group,
                                     const QPointF     &click,
                                     const QPointF     &dir,
                                     bool               trimHighEnd) {
        const double len = QLineF(QPointF(0, 0), dir).length();
        if (len < 1e-10) return;
        const QPointF nd = dir / len;
        const double  cp = QPointF::dotProduct(click, nd);

        // 클릭 투영이 세그먼트 범위에서 얼마나 벗어났는지 (0 = 범위 내)
        auto projDist = [&](LineItem *li) -> double {
            const QLineF  q  = toLineF(li);
            const double  p1 = QPointF::dotProduct(q.p1(), nd);
            const double  p2 = QPointF::dotProduct(q.p2(), nd);
            const double  lo = qMin(p1, p2), hi = qMax(p1, p2);
            return (cp < lo) ? (lo - cp) : (cp > hi) ? (cp - hi) : 0.0;
        };
        auto trimVal = [&](LineItem *li) -> double {
            const QLineF  q  = toLineF(li);
            const double  p1 = QPointF::dotProduct(q.p1(), nd);
            const double  p2 = QPointF::dotProduct(q.p2(), nd);
            return trimHighEnd ? qMax(p1, p2) : -qMin(p1, p2);
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
            double    bestT = trimVal(group[i]);
            for (int j = i + 1; j < group.size(); ++j) {
                if (used[j] || !isCollinear(group[i], group[j])) continue;
                used[j] = true;
                const double d = projDist(group[j]);
                const double t = trimVal(group[j]);
                if (d < bestD - 0.01 || (d < bestD + 0.01 && t > bestT)) {
                    bestD = d; bestT = t; best = group[j];
                }
            }
            result << best;
        }
        if (!result.isEmpty()) group = result;
    };

    deduplicateByClick(groupA, clickA, aDir, trimAHighEnd);
    deduplicateByClick(groupB, clickB, bDir, trimBHighEnd);

    // ── 같은 인덱스끼리 매칭 → 교점 계산 → 트림 ─────────────────────────────
    struct TrimOp {
        LineItem *line;
        QPointF   oldPos;
        QPointF   newPos;
        bool      isFirstEnd;
    };
    QList<TrimOp> ops;

    const int n = qMin(groupA.size(), groupB.size());
    for (int i = 0; i < n; ++i) {
        LineItem *la  = groupA[i];
        LineItem *lb  = groupB[i];
        const QLineF qla = toLineF(la);
        const QLineF qlb = toLineF(lb);

        QPointF P;
        if (qla.intersects(qlb, &P) == QLineF::NoIntersection) continue;

        // la 트림 끝: 교점 P 기준 — 클릭이 있는 쪽(P 반대편)을 유지
        // (pA1<pA2)==(click>P) : click이 HIGH쪽이면 LOW끝(p1)을 트림, 반대면 HIGH끝(p2) 트림
        const double pA1    = QPointF::dotProduct(qla.p1(), aDir);
        const double pA2    = QPointF::dotProduct(qla.p2(), aDir);
        const double cpA_cl = QPointF::dotProduct(clickA, aDir);
        const double cpA_P  = QPointF::dotProduct(P,      aDir);
        const bool   aAtP1  = (pA1 < pA2) == (cpA_cl > cpA_P);
        ops << TrimOp{ la, aAtP1 ? qla.p1() : qla.p2(), P, aAtP1 };

        // lb 트림 끝: 동일 원리
        const double pB1    = QPointF::dotProduct(qlb.p1(), bDir);
        const double pB2    = QPointF::dotProduct(qlb.p2(), bDir);
        const double cpB_cl = QPointF::dotProduct(clickB, bDir);
        const double cpB_P  = QPointF::dotProduct(P,      bDir);
        const bool   bAtP1  = (pB1 < pB2) == (cpB_cl > cpB_P);
        ops << TrimOp{ lb, bAtP1 ? qlb.p1() : qlb.p2(), P, bAtP1 };
    }

    // ── Undo/Redo 적용 ────────────────────────────────────────────────────────
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->undoStack() || ops.isEmpty()) {
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(
                tr("L-Corner: 교점을 찾을 수 없습니다."), CommandDock::Info);
        return;
    }

    ctx->undoStack()->beginMacro("Multiline L-Corner");
    for (int i = 0; i < ops.size(); ++i) {
        const TrimOp &op = ops.at(i);
        ctx->undoStack()->push(
            new ExtendEntityCommand(op.line, op.oldPos, op.newPos, op.isFirstEnd));
    }
    ctx->undoStack()->endMacro();

    if (ctx->commandDock())
        ctx->commandDock()->logMessage(
            tr("L-Corner 완료: %1개 선 정리됨.").arg(ops.size()),
            CommandDock::Info);
}

// ── MultilineCornerCommand ────────────────────────────────────────────────────
MultilineCornerCommand::MultilineCornerCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx)
{}

void MultilineCornerCommand::execute()
{
    if (!m_ctx) return;
    m_ctx->startTool(new MultilineCornerTool());
}
