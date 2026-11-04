#include "jointool.h"
#include "arcitem.h"
#include "cadcommands.h"
#include "caditem.h"
#include "circleitem.h"
#include "commanddock.h"
#include "gripitem.h"
#include "iappcontext.h"
#include "lineitem.h"
#include "multilinedata.h"
#include "myarc.h"
#include "mycircle.h"
#include "mygraphicsscene.h"
#include "myline.h"
#include "myvector.h"

#include <QKeyEvent>
#include <QMap>
#include <QPair>
#include <QUndoStack>
//#include <algorithm>
#include <cmath>

// ── 선분 판정 허용값 ─────────────────────────────────────────────────────────
static constexpr double PARALLEL_EPS   = 1e-6;   // 평행 판정 (cos 오차)
static constexpr double COLLINEAR_DIST = 0.5;    // 동일 무한선 수직거리 허용

// ── 호 판정 허용값 ───────────────────────────────────────────────────────────
static constexpr double CENTER_EPS = 1.0;    // 중심 거리 허용
static constexpr double RADIUS_EPS = 1.0;    // 반지름 차이 허용
static constexpr double ANGLE_EPS  = 0.05;   // 각도 맞닿음 허용 (도)

// ─────────────────────────────────────────────────────────────────────────────
JoinTool::JoinTool(QObject *parent)
    : AbstractTool(parent), m_isBoxSelecting(false) {}

JoinTool::~JoinTool() {}

// ─────────────────────────────────────────────────────────────────────────────
void JoinTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene)
        return;
    AbstractTool::activate(scene);
    m_isBoxSelecting = false;
    hideGripsOfSelectedItems();
    showPrompt();
}

void JoinTool::deactivate() {
    if (m_scene)
        m_scene->clearOverlay();
    m_isBoxSelecting = false;
    AbstractTool::deactivate();
}

void JoinTool::showPrompt() const {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage("JOIN", CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(tr(" 연결할 객체를 선택 하세요 : "), {});
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void JoinTool::mousePress(QPointF pos, Qt::MouseButton button) {
    handleSelectionPress(pos, button);
}

void JoinTool::mouseMove(QPointF pos) {
    handleSelectionMove(pos);
}

void JoinTool::mouseRelease(QPointF /*pos*/, Qt::MouseButton /*button*/) {}

void JoinTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        if (m_scene)
            m_scene->clearSelection();
        emit toolFinished();
    } else {
        AbstractTool::keyPress(event);
    }
}

bool JoinTool::processCommand(const QString &command) {
    const QString cmd = command.trimmed().toLower();
    if (cmd == "p") {
        if (m_scene && m_scene->applyPreviousSelection())
            hideGripsOfSelectedItems();
        return true;
    }
    if (cmd.isEmpty()) {
        executeJoin();
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
void JoinTool::handleSelectionPress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene)
        return;

    if (button == Qt::RightButton) {
        executeJoin();
        return;
    }
    if (button != Qt::LeftButton)
        return;

    if (!m_isBoxSelecting) {
        QGraphicsItem *item = itemAtPos(pos);
        if (item) {
            if (item->type() == GripItem::Type && item->parentItem())
                item = item->parentItem();
            if (item->type() != GripItem::Type) {
                item->setSelected(!item->isSelected());
                hideGripsOfSelectedItems();
                return;
            }
        }
        m_isBoxSelecting = true;
        m_firstCorner    = pos;
    } else {
        processBoxSelection(pos);
        m_isBoxSelecting = false;
        m_scene->clearOverlay();
    }
}

void JoinTool::handleSelectionMove(QPointF pos) {
    if (m_isBoxSelecting && m_scene) {
        MyGraphicsScene::SelectionMode mode = (pos.x() > m_firstCorner.x())
                                              ? MyGraphicsScene::Mode_Window
                                              : MyGraphicsScene::Mode_Crossing;
        m_scene->setSelectionRect(m_firstCorner, pos, mode);
    }
}

void JoinTool::processBoxSelection(QPointF p2) {
    bool isWindow = (p2.x() > m_firstCorner.x());
    QList<CadItem *> topItems = isWindow
        ? collectItemsInWindowRect(m_firstCorner, p2)
        : collectItemsInCrossingRect(m_firstCorner, p2);
    for (int i = 0; i < topItems.size(); ++i)
        topItems.at(i)->setSelected(true);
    hideGripsOfSelectedItems();
}

// ─────────────────────────────────────────────────────────────────────────────
// 선분 헬퍼
// ─────────────────────────────────────────────────────────────────────────────
bool JoinTool::linesAreCollinear(LineItem *a, LineItem *b) {
    MyLine la = a->getLine();
    MyLine lb = b->getLine();

    MyVector da = (la.end - la.start).normalized();
    MyVector db = (lb.end - lb.start).normalized();

    double dot = da.x * db.x + da.y * db.y;
    if (std::abs(std::abs(dot) - 1.0) > PARALLEL_EPS * 1000.0)
        return false;

    MyVector d = lb.start - la.start;
    double cross = da.x * d.y - da.y * d.x;
    return std::abs(cross) < COLLINEAR_DIST;
}

LineItem *JoinTool::mergeLineGroup(const QList<LineItem *> &group) {
    if (group.size() < 2)
        return nullptr;

    MyLine la    = group[0]->getLine();
    MyVector dir = (la.end - la.start).normalized();
    MyVector anc = la.start;

    double tMin = 0.0;
    double tMax = (la.end - la.start).magnitude();

    for (int i = 0; i < group.size(); ++i) {
        MyLine li    = group[i]->getLine();
        MyVector ps  = li.start - anc;
        MyVector pe  = li.end   - anc;
        double ts    = ps.x * dir.x + ps.y * dir.y;
        double te    = pe.x * dir.x + pe.y * dir.y;
        if (ts < tMin) tMin = ts;
        if (ts > tMax) tMax = ts;
        if (te < tMin) tMin = te;
        if (te > tMax) tMax = te;
    }

    return new LineItem(anc + dir * tMin, anc + dir * tMax);
}

// ─────────────────────────────────────────────────────────────────────────────
// 호 헬퍼
// ─────────────────────────────────────────────────────────────────────────────
bool JoinTool::arcsOnSameCircle(ArcItem *a, ArcItem *b) {
    MyArc la = a->getArc();
    MyArc lb = b->getArc();
    return la.center.distanceTo(lb.center) < CENTER_EPS &&
           std::abs(la.radius - lb.radius) < RADIUS_EPS;
}

// mergeArcAngles: 선택된 모든 호를 하나의 최소 스패닝 호로 합침 (갭 있어도 허용).
// 최대 갭을 찾아 나머지를 하나의 호로 만드는 방식 — 선분 min~max 방식과 동일 개념.
// 반환: {startAngle, spanAngle} 쌍 (항상 크기 0 또는 1).
//   - 빈 목록 = 360° 이상 → 원으로 전환
QList<QPair<double,double>> JoinTool::mergeArcAngles(const QList<ArcItem *> &group) {
    struct Iv { double s, e; };
    QList<Iv> ivs;
    ivs.reserve(group.size());

    for (int i = 0; i < group.size(); ++i) {
        MyArc arc = group[i]->getArc();
        double s  = std::fmod(arc.startAngle, 360.0);
        if (s < 0.0) s += 360.0;
        double sp = std::abs(arc.spanAngle);
        if (sp > 360.0) sp = 360.0;
        if (sp >= 360.0 - ANGLE_EPS)
            return {};                  // 이미 원
        ivs.append({s, s + sp});
    }

    // 시작각 기준 정렬
    for (int i = 0; i < ivs.size() - 1; ++i)
        for (int j = i + 1; j < ivs.size(); ++j)
            if (ivs[j].s < ivs[i].s)
                qSwap(ivs[i], ivs[j]);

    // 겹치는/맞닿는 구간 1차 합병 (순방향)
    QList<Iv> merged;
    merged.reserve(ivs.size());
    Iv cur = ivs[0];
    for (int i = 1; i < ivs.size(); ++i) {
        if (ivs[i].s <= cur.e + ANGLE_EPS)
            cur.e = qMax(cur.e, ivs[i].e);
        else {
            merged.append(cur);
            cur = ivs[i];
        }
    }
    merged.append(cur);

    // 마지막 구간이 첫 구간과 원형으로 연결되는지 확인
    if (merged.size() > 1) {
        double wrapOverlap = merged.last().e - (merged[0].s + 360.0);
        if (wrapOverlap >= -ANGLE_EPS) {
            // 마지막과 첫 번째가 연결 → 원 또는 단일 구간
            double newEnd  = qMax(merged.last().e, merged[0].e + 360.0);
            double newSpan = newEnd - merged.last().s;
            if (newSpan >= 360.0 - ANGLE_EPS)
                return {};
            double ns = std::fmod(merged.last().s, 360.0);
            if (ns < 0.0) ns += 360.0;
            merged.removeFirst();
            merged.last() = {ns, ns + newSpan};
        }
    }

    // ── 핵심: 갭이 있어도 최소 스패닝 호 생성 ───────────────────────────
    // 구간이 1개로 합쳐졌으면 그것이 최종 결과
    if (merged.size() == 1) {
        double s    = std::fmod(merged[0].s, 360.0);
        if (s < 0.0) s += 360.0;
        double span = merged[0].e - merged[0].s;
        if (span >= 360.0 - ANGLE_EPS) return {};
        return {qMakePair(s, span)};
    }

    // 구간이 2개 이상 남았으면(갭 존재): 최대 갭을 건너뛰고 나머지를 하나의 호로
    int    largestIdx  = merged.size() - 1; // 초기값: 원형 갭(마지막→첫번째)
    double largestSize = merged[0].s + 360.0 - merged.last().e; // 원형 갭 크기

    for (int i = 0; i < merged.size() - 1; ++i) {
        double gap = merged[i + 1].s - merged[i].e;
        if (gap > largestSize) {
            largestSize = gap;
            largestIdx  = i;
        }
    }

    if (largestSize < ANGLE_EPS) return {}; // 실질적으로 원

    double span = 360.0 - largestSize;
    if (span >= 360.0 - ANGLE_EPS) return {};

    // 최대 갭 직후 구간의 시작점에서 호가 시작
    double arcStart;
    if (largestIdx == merged.size() - 1) {
        // 원형 갭이 최대 → 첫 구간 시작점에서 출발
        arcStart = std::fmod(merged[0].s, 360.0);
    } else {
        arcStart = std::fmod(merged[largestIdx + 1].s, 360.0);
    }
    if (arcStart < 0.0) arcStart += 360.0;

    return {qMakePair(arcStart, span)};
}

// ─────────────────────────────────────────────────────────────────────────────
// executeJoin
// ─────────────────────────────────────────────────────────────────────────────
void JoinTool::executeJoin() {
    if (!m_scene) {
        emit toolFinished();
        return;
    }

    m_scene->savePreviousSelection();

    QList<QGraphicsItem *> sel = m_scene->selectedItems();

    // 타입별 분류
    QList<LineItem *> lines;
    QList<ArcItem  *> arcs;
    for (int i = 0; i < sel.size(); ++i) {
        if (LineItem *li = dynamic_cast<LineItem *>(sel[i]))
            lines.append(li);
        else if (ArcItem *ai = dynamic_cast<ArcItem *>(sel[i]))
            arcs.append(ai);
    }

    if (lines.isEmpty() && arcs.isEmpty()) {
        emit toolFinished();
        return;
    }

    IAppContext *ctx   = getContext();
    QUndoStack  *stack = ctx ? ctx->undoStack() : nullptr;
    int mergedCount    = 0;

    if (stack)
        stack->beginMacro(tr("Join"));

    // ── 일반 선분 ────────────────────────────────────────────────────────────
    QMap<QString, QList<LineItem *>> mlGroups;
    QList<LineItem *> regularLines;
    for (int i = 0; i < lines.size(); ++i) {
        LineItem *li = lines[i];
        QString gid  = li->data(ML_DATA_GROUP_ID).toString();
        if (!gid.isEmpty())
            mlGroups[gid].append(li);
        else
            regularLines.append(li);
    }

    QList<bool> done(regularLines.size(), false);
    for (int i = 0; i < regularLines.size(); ++i) {
        if (done[i]) continue;
        QList<LineItem *> grp;
        grp.append(regularLines[i]);
        for (int j = i + 1; j < regularLines.size(); ++j) {
            if (!done[j] && linesAreCollinear(regularLines[i], regularLines[j])) {
                grp.append(regularLines[j]);
                done[j] = true;
            }
        }
        done[i] = true;
        if (grp.size() < 2) continue;

        LineItem *merged = mergeLineGroup(grp);
        if (!merged) continue;

        LineItem *ref = grp[0];
        merged->setPen(ref->pen());
        merged->setLayerName(ref->layerName());

        for (int k = 0; k < grp.size(); ++k) {
            if (stack) stack->push(new RemoveEntityCommand(m_scene, grp[k]));
            else { m_scene->removeItem(grp[k]); delete grp[k]; }
        }
        if (stack) stack->push(new AddEntityCommand(m_scene, merged));
        else m_scene->addItem(merged);

        mergedCount += grp.size() - 1;
    }

    // ── 다중선 ───────────────────────────────────────────────────────────────
    QList<QString> gids = mlGroups.keys();
    for (int g = 0; g < gids.size(); ++g) {
        const QString &gid         = gids[g];
        QList<LineItem *> groupLines = mlGroups[gid];

        QList<bool> mlDone(groupLines.size(), false);
        for (int i = 0; i < groupLines.size(); ++i) {
            if (mlDone[i]) continue;
            QList<LineItem *> sub;
            sub.append(groupLines[i]);
            for (int j = i + 1; j < groupLines.size(); ++j) {
                if (!mlDone[j] && linesAreCollinear(groupLines[i], groupLines[j])) {
                    sub.append(groupLines[j]);
                    mlDone[j] = true;
                }
            }
            mlDone[i] = true;
            if (sub.size() < 2) continue;

            LineItem *merged = mergeLineGroup(sub);
            if (!merged) continue;

            LineItem *ref = sub[0];
            merged->setPen(ref->pen());
            merged->setLayerName(ref->layerName());
            merged->setData(ML_DATA_GROUP_ID, gid);
            merged->setData(ML_DATA_ROLE,     ref->data(ML_DATA_ROLE));
            double sp = ref->data(ML_DATA_SPACING).toDouble();
            if (sp > 0.0) merged->setData(ML_DATA_SPACING, sp);

            for (int k = 0; k < sub.size(); ++k) {
                if (stack) stack->push(new RemoveEntityCommand(m_scene, sub[k]));
                else { m_scene->removeItem(sub[k]); delete sub[k]; }
            }
            if (stack) stack->push(new AddEntityCommand(m_scene, merged));
            else m_scene->addItem(merged);

            mergedCount += sub.size() - 1;
        }
    }

    // ── 호 ───────────────────────────────────────────────────────────────────
    // 같은 원 위의 호끼리 그룹화
    QList<bool> arcDone(arcs.size(), false);
    for (int i = 0; i < arcs.size(); ++i) {
        if (arcDone[i]) continue;
        QList<ArcItem *> grp;
        grp.append(arcs[i]);
        arcDone[i] = true;
        for (int j = i + 1; j < arcs.size(); ++j) {
            if (!arcDone[j] && arcsOnSameCircle(arcs[i], arcs[j])) {
                grp.append(arcs[j]);
                arcDone[j] = true;
            }
        }
        // 호 1개 선택 → 원으로 닫기
        if (grp.size() == 1) {
            ArcItem *ref = grp[0];
            MyArc refArc = ref->getArc();
            CircleItem *ci = new CircleItem(refArc.center, refArc.radius);
            ci->setPen(ref->pen());
            ci->setLayerName(ref->layerName());
            if (stack) {
                stack->push(new RemoveEntityCommand(m_scene, ref));
                stack->push(new AddEntityCommand(m_scene, ci));
            } else {
                m_scene->removeItem(ref);
                delete ref;
                m_scene->addItem(ci);
            }
            ++mergedCount;
            continue;
        }

        QList<QPair<double,double>> merged = mergeArcAngles(grp);

        // 병합 결과가 원래 수 이상이면 실제로 합쳐진 것이 없음
        bool noMerge = !merged.isEmpty() && (merged.size() >= grp.size());
        if (noMerge) continue;

        ArcItem *ref  = grp[0];
        MyArc refArc  = ref->getArc();

        for (int k = 0; k < grp.size(); ++k) {
            if (stack) stack->push(new RemoveEntityCommand(m_scene, grp[k]));
            else { m_scene->removeItem(grp[k]); delete grp[k]; }
        }

        if (merged.isEmpty()) {
            // 360° → 원
            CircleItem *ci = new CircleItem(refArc.center, refArc.radius);
            ci->setPen(ref->pen());
            ci->setLayerName(ref->layerName());
            if (stack) stack->push(new AddEntityCommand(m_scene, ci));
            else m_scene->addItem(ci);
            mergedCount += grp.size() - 1;
        } else {
            // 부분 병합 → 호 목록
            for (int m = 0; m < merged.size(); ++m) {
                ArcItem *ai = new ArcItem(
                    MyArc(refArc.center, refArc.radius,
                          merged[m].first, merged[m].second));
                ai->setPen(ref->pen());
                ai->setLayerName(ref->layerName());
                if (stack) stack->push(new AddEntityCommand(m_scene, ai));
                else m_scene->addItem(ai);
            }
            mergedCount += grp.size() - merged.size();
        }
    }

    if (stack)
        stack->endMacro();

    if (ctx && ctx->commandDock()) {
        if (mergedCount > 0)
            ctx->commandDock()->logMessage(
                tr("%1개 객체가 연결되었습니다.").arg(mergedCount), CommandDock::Info);
        else
            ctx->commandDock()->logMessage(
                tr("연결 가능한 객체가 없습니다."), CommandDock::Info);
        ctx->commandDock()->setPrompt(" Command : ");
    }

    m_scene->clearSelection();
    emit toolFinished();
}
