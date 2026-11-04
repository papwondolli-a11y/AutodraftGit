#include "areacornerjointool.h"
#include "multilinedata.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "lineitem.h"
#include "myline.h"
#include "mygraphicsscene.h"

#include <QGraphicsView>
#include <QUndoStack>
#include <cmath>
#include <algorithm>

// ── ctor ─────────────────────────────────────────────────────────────────────
AreaCornerJoinTool::AreaCornerJoinTool(JoinMode mode, QObject *parent)
    : AbstractTool(parent),
      m_mode(mode),
      m_currentState(State_WaitingForFirst),
      m_isSelecting(false)
{}

// ── activate / deactivate ────────────────────────────────────────────────────
void AreaCornerJoinTool::activate(MyGraphicsScene *scene)
{
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    resetState();

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        QString modeStr = (m_mode == LCorner) ? "L-CORNER" : (m_mode == TCorner) ? "T-CORNER" : "C-CORNER";
        ctx->commandDock()->logMessage(QString("AREA %1 ACTIVE").arg(modeStr), CommandDock::Command);
    }
    showPrompt();
}

void AreaCornerJoinTool::deactivate()
{
    resetState();
    if (m_scene) {
        m_scene->clearOverlay();
    }
    AbstractTool::deactivate();
}

// ── helpers ───────────────────────────────────────────────────────────────────
void AreaCornerJoinTool::resetState()
{
    m_currentState = State_WaitingForFirst;
    m_isSelecting = false;
}

void AreaCornerJoinTool::showPrompt() const
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    QString modeStr = (m_mode == LCorner) ? tr("[L-코너]") : (m_mode == TCorner) ? tr("[T-코너]") : tr("[C-코너]");
    
    if (m_currentState == State_WaitingForFirst) {
        ctx->commandDock()->showInteractivePrompt(tr(" 영역 %1: 첫 번째 구석(1) 클릭 ").arg(modeStr), {});
    } else {
        ctx->commandDock()->showInteractivePrompt(tr(" 영역 %1: 반대쪽 구석(2) 클릭 ").arg(modeStr), {});
    }
}

QLineF AreaCornerJoinTool::toLineF(LineItem *li) const
{
    MyLine d = li->getLine();
    return QLineF(d.start.toPointF(), d.end.toPointF());
}

bool AreaCornerJoinTool::getIntersection(const QLineF &l1, const QLineF &l2, QPointF &rP) const
{
    const double denom = (l2.y2() - l2.y1()) * (l1.x2() - l1.x1()) - (l2.x2() - l2.x1()) * (l1.y2() - l1.y1());
    if (std::abs(denom) < 1e-9) return false;
    const double num = (l2.x2() - l2.x1()) * (l1.y1() - l2.y1()) - (l2.y2() - l2.y1()) * (l1.x1() - l2.x1());
    const double t = num / denom;
    rP = QPointF(l1.x1() + t * (l1.x2() - l1.x1()), l1.y1() + t * (l1.y2() - l1.y1()));
    return true;
}

double AreaCornerJoinTool::getProjT(const QPointF &pt, const QLineF &seg) const
{
    const double len = seg.length();
    if (len < 1e-9) return 0.0;
    return QPointF::dotProduct(pt - seg.p1(), seg.p2() - seg.p1()) / (len * len);
}

// ── mouse ─────────────────────────────────────────────────────────────────────
void AreaCornerJoinTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (button == Qt::RightButton) {
        resetState();
        if (m_scene) m_scene->clearOverlay();
        showPrompt();
        return;
    }
    if (button != Qt::LeftButton) return;

    if (m_currentState == State_WaitingForFirst) {
        m_firstCorner = pos;
        m_isSelecting = true;
        m_currentState = State_WaitingForSecond;
        showPrompt();
    } else {
        QRectF rect = QRectF(m_firstCorner, pos).normalized();
        doAreaJoin(rect);
        resetState();
        if (m_scene) m_scene->clearOverlay();
        showPrompt();
    }
}

void AreaCornerJoinTool::mouseMove(QPointF pos)
{
    if (m_isSelecting && m_scene) {
        m_scene->setSelectionRect(m_firstCorner, pos, MyGraphicsScene::Mode_Crossing);
    }
}

void AreaCornerJoinTool::mouseRelease(QPointF /*pos*/, Qt::MouseButton /*button*/)
{}

// ── 영역 조인 연산 실행 ───────────────────────────────────────────────────────
void AreaCornerJoinTool::doAreaJoin(const QRectF &rect)
{
    if (!m_scene) return;

    // 1. 사각형 영역 내 모든 LineItem 추출
    QList<LineItem *> rawLines;
    QList<QGraphicsItem *> items = m_scene->items(rect);
    const int itemCount = items.size();
    for (int i = 0; i < itemCount; ++i) {
        LineItem *li = dynamic_cast<LineItem *>(items.at(i));
        if (li) {
            rawLines << li;
        }
    }

    if (rawLines.size() < 2) {
        IAppContext *ctx = getContext();
        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->logMessage(tr("영역 내에 2개 이상의 선이 없습니다."), CommandDock::Info);
        }
        return;
    }

    // 2. 가로선(HGroup)과 세로선(VGroup)으로 분류
    QList<LineItem *> hGroup;
    QList<LineItem *> vGroup;

    const int rawLineCount = rawLines.size();
    for (int i = 0; i < rawLineCount; ++i) {
        LineItem *li = rawLines.at(i);
        QLineF lf = toLineF(li);
        double angle = std::abs(lf.angle()); // 0 ~ 360
        double angleMod = std::fmod(angle, 180.0);
        if (angleMod < 20.0 || angleMod > 160.0) {
            hGroup << li;
        } else if (angleMod > 70.0 && angleMod < 110.0) {
            vGroup << li;
        }
    }

    if (hGroup.isEmpty() || vGroup.isEmpty()) return;

    // 3. 기하학적 정렬
    std::sort(hGroup.begin(), hGroup.end(), [this](LineItem *a, LineItem *b) {
        return toLineF(a).p1().y() < toLineF(b).p1().y();
    });
    std::sort(vGroup.begin(), vGroup.end(), [this](LineItem *a, LineItem *b) {
        return toLineF(a).p1().x() < toLineF(b).p1().x();
    });

    QList<Op> ops;
    const int matchCount = std::min(hGroup.size(), vGroup.size());

    // ── [L-코너 모드] ──────────────────────────────────────────────────────────
    if (m_mode == LCorner) {
        // 교점의 평균 좌표 계산
        double sumX = 0.0, sumY = 0.0;
        for (int i = 0; i < hGroup.size(); ++i) {
            sumY += toLineF(hGroup.at(i)).p1().y();
        }
        for (int i = 0; i < vGroup.size(); ++i) {
            sumX += toLineF(vGroup.at(i)).p1().x();
        }
        const QPointF P_avg(sumX / vGroup.size(), sumY / hGroup.size());
        const QPointF rectCenter = rect.center();

        // 남겨질 코너 방향 판별 (사각형의 반대 방향)
        const bool isLeft = (rectCenter.x() > P_avg.x());
        const bool isTop  = (rectCenter.y() > P_avg.y());

        // true-true (Left-Top) / false-false (Right-Bottom) ➔ 정순 매칭 (0-0, 1-1)
        // true-false (Left-Bottom) / false-true (Right-Top) ➔ 역순 매칭 (0-1, 1-0)
        const bool useDirectOrder = (isLeft == isTop);

        for (int i = 0; i < matchCount; ++i) {
            LineItem *la = hGroup.at(i);
            const int vIdx = useDirectOrder ? i : (matchCount - 1 - i);
            if (vIdx < 0 || vIdx >= vGroup.size()) continue;
            LineItem *lb = vGroup.at(vIdx);

            const QLineF qla = toLineF(la);
            const QLineF qlb = toLineF(lb);

            QPointF P;
            if (!getIntersection(qla, qlb, P)) continue;
            if (!rect.contains(P)) continue;

            // 사각형 영역 내에 들어간 끝점을 교점 P로 이동하여 삐져나간 부분 삭제
            const bool aP1InRect = rect.contains(qla.p1());
            const bool aP2InRect = rect.contains(qla.p2());
            if (aP1InRect != aP2InRect) {
                ops << Op{ la, aP1InRect ? qla.p1() : qla.p2(), P, aP1InRect, nullptr };
            } else {
                const bool nearP1 = QLineF(qla.p1(), P).length() < QLineF(qla.p2(), P).length();
                ops << Op{ la, nearP1 ? qla.p1() : qla.p2(), P, nearP1, nullptr };
            }

            const bool bP1InRect = rect.contains(qlb.p1());
            const bool bP2InRect = rect.contains(qlb.p2());
            if (bP1InRect != bP2InRect) {
                ops << Op{ lb, bP1InRect ? qlb.p1() : qlb.p2(), P, bP1InRect, nullptr };
            } else {
                const bool nearP1 = QLineF(qlb.p1(), P).length() < QLineF(qlb.p2(), P).length();
                ops << Op{ lb, nearP1 ? qlb.p1() : qlb.p2(), P, nearP1, nullptr };
            }
        }
    }
    // ── [T-코너 모드] ──────────────────────────────────────────────────────────
    else if (m_mode == TCorner) {
        // 교차선(Crossbar) / 줄기선(Stem) 그룹 판별
        double sumX = 0.0, sumY = 0.0;
        for (int i = 0; i < hGroup.size(); ++i) sumY += toLineF(hGroup.at(i)).p1().y();
        for (int i = 0; i < vGroup.size(); ++i) sumX += toLineF(vGroup.at(i)).p1().x();
        const QPointF P_avg(sumX / vGroup.size(), sumY / hGroup.size());

        // 각 그룹의 교점 투영 비율 t의 평균값 계산 (P_avg가 선분의 어디에 오는지)
        double tH_sum = 0.0;
        for (int i = 0; i < hGroup.size(); ++i) {
            tH_sum += getProjT(P_avg, toLineF(hGroup.at(i)));
        }
        double tV_sum = 0.0;
        for (int i = 0; i < vGroup.size(); ++i) {
            tV_sum += getProjT(P_avg, toLineF(vGroup.at(i)));
        }
        double tH_avg = tH_sum / hGroup.size();
        double tV_avg = tV_sum / vGroup.size();

        // t가 0.5에 더 가까운 그룹이 교차선(Crossbar)이고, 다른 그룹이 줄기선(Stem)
        const bool hIsCrossbar = (std::abs(tH_avg - 0.5) < std::abs(tV_avg - 0.5));

        QList<LineItem *> crossbarGroup = hIsCrossbar ? hGroup : vGroup;
        QList<LineItem *> stemGroup     = hIsCrossbar ? vGroup : hGroup;

        // 실제로 트림이 발생한 줄기선들을 수집하는 리스트
        QList<LineItem *> trimmedStems;

        // 가까운 면(nearIdx)과 먼 면(farIdx) 인덱스 계산
        int nearIdx = 0;
        int farIdx = 1;
        const QPointF rectCenter = rect.center();
        
        if (hIsCrossbar) {
            if (rectCenter.y() > P_avg.y()) {
                nearIdx = 1;
                farIdx = 0;
            } else {
                nearIdx = 0;
                farIdx = 1;
            }
        } else {
            if (rectCenter.x() > P_avg.x()) {
                nearIdx = 1;
                farIdx = 0;
            } else {
                nearIdx = 0;
                farIdx = 1;
            }
        }
        const int maxCrossIdx = crossbarGroup.size() - 1;
        nearIdx = std::max(0, std::min(nearIdx, maxCrossIdx));
        farIdx  = std::max(0, std::min(farIdx, maxCrossIdx));

        const int stemCount = stemGroup.size();
        const int crossbarCount = crossbarGroup.size();

        // 각 줄기선별로 가장 적합한 교차선 매칭 수행 (색상별 기하 깊이 비례 매칭)
        QVector<int> stemToCross(stemCount, -1);

        // 1. 색상별로 줄기선과 교차선 분류 (RGB 비교로 오차 방지)
        QList<QColor> colors;
        for (int i = 0; i < stemCount; ++i) {
            QColor col = stemGroup.at(i)->pen().color();
            bool alreadyHas = false;
            for (int c = 0; c < colors.size(); ++c) {
                if (colors.at(c).rgb() == col.rgb()) {
                    alreadyHas = true;
                    break;
                }
            }
            if (!alreadyHas) {
                colors.append(col);
            }
        }

        // 각 색상별로 매칭 실행
        for (int cIdx = 0; cIdx < colors.size(); ++cIdx) {
            QColor curColor = colors.at(cIdx);

            struct StemInfo {
                int originalIdx;
                LineItem *item;
            };
            QList<StemInfo> curStems;
            for (int i = 0; i < stemCount; ++i) {
                if (stemGroup.at(i)->pen().color().rgb() == curColor.rgb()) {
                    curStems.append(StemInfo{ i, stemGroup.at(i) });
                }
            }

            struct CrossInfo {
                int originalIdx;
                LineItem *item;
            };
            QList<CrossInfo> curCrossbars;
            for (int j = 0; j < crossbarCount; ++j) {
                if (crossbarGroup.at(j)->pen().color().rgb() == curColor.rgb()) {
                    curCrossbars.append(CrossInfo{ j, crossbarGroup.at(j) });
                }
            }

            if (curStems.isEmpty() || curCrossbars.isEmpty()) continue;

            // 교차선들을 먼 면(farIdx)이 가장 앞에 오도록 정렬 (nearIdx와 farIdx 관계 기준)
            bool reverseCross = (farIdx > nearIdx);
            if (reverseCross) {
                std::sort(curCrossbars.begin(), curCrossbars.end(), [](const CrossInfo &a, const CrossInfo &b) {
                    return a.originalIdx > b.originalIdx;
                });
            } else {
                std::sort(curCrossbars.begin(), curCrossbars.end(), [](const CrossInfo &a, const CrossInfo &b) {
                    return a.originalIdx < b.originalIdx;
                });
            }

            const int K = curStems.size();
            const int L = curCrossbars.size();

            for (int k = 0; k < K; ++k) {
                int targetJ = 0;
                if (K > 1) {
                    // 기하학적 꼬임 방지를 위한 완벽한 역순 비례 매핑 (모든 다중선 벽체 호환)
                    targetJ = qRound(double(K - 1 - k) / (K - 1) * (L - 1));
                } else {
                    targetJ = 0;
                }
                targetJ = qMax(0, qMin(targetJ, L - 1));

                int origStemIdx = curStems.at(k).originalIdx;
                int origCrossIdx = curCrossbars.at(targetJ).originalIdx;
                stemToCross[origStemIdx] = origCrossIdx;
            }
        }

        // 매칭 결과 바탕으로 트림 연산 데이터 준비 (stem 연장/트림)
        for (int i = 0; i < stemCount; ++i) {
            int targetCrossIdx = stemToCross[i];
            
            // 예외 처리: 매칭되지 않은 줄기선이 존재할 경우 가장 가까운 것 할당
            if (targetCrossIdx == -1) {
                double minDist = -1.0;
                int bestCross = -1;
                LineItem *stem = stemGroup.at(i);
                const QLineF qStem = toLineF(stem);
                QPointF E;
                const bool p1In = rect.contains(qStem.p1());
                const bool p2In = rect.contains(qStem.p2());
                if (p1In != p2In) E = p1In ? qStem.p1() : qStem.p2();
                else E = (QLineF(qStem.p1(), rect.center()).length() < QLineF(qStem.p2(), rect.center()).length()) ? qStem.p1() : qStem.p2();

                for (int j = 0; j < crossbarCount; ++j) {
                    QPointF P;
                    if (getIntersection(qStem, toLineF(crossbarGroup.at(j)), P)) {
                        double dist = QLineF(E, P).length();
                        if (minDist < 0.0 || dist < minDist) {
                            minDist = dist;
                            bestCross = j;
                        }
                    }
                }
                if (bestCross != -1) {
                    targetCrossIdx = bestCross;
                    stemToCross[i] = bestCross;
                }
            }

            if (targetCrossIdx < 0 || targetCrossIdx >= crossbarCount) continue;
            
            LineItem *stem = stemGroup.at(i);
            const QLineF qStem = toLineF(stem);
            LineItem *targetCross = crossbarGroup.at(targetCrossIdx);

            QPointF P;
            if (getIntersection(qStem, toLineF(targetCross), P)) {
                if (rect.contains(P)) {
                    const bool aP1InRect = rect.contains(qStem.p1());
                    const bool aP2InRect = rect.contains(qStem.p2());
                    if (aP1InRect != aP2InRect) {
                        ops << Op{ stem, aP1InRect ? qStem.p1() : qStem.p2(), P, aP1InRect, nullptr };
                        trimmedStems << stem;
                    } else {
                        const bool nearP1 = QLineF(qStem.p1(), P).length() < QLineF(qStem.p2(), P).length();
                        ops << Op{ stem, nearP1 ? qStem.p1() : qStem.p2(), P, nearP1, nullptr };
                        trimmedStems << stem;
                    }
                }
            }
        }

        // 교차선들 중 안쪽 교차선들 분할 연산
        for (int i = 0; i < crossbarGroup.size(); ++i) {
            // 외벽선(맨 위 0번째 선과 맨 아래 마지막 선)은 절대로 분할하지 않고 통과시킴
            if (i == 0 || i == crossbarGroup.size() - 1) continue;

            LineItem *cross = crossbarGroup.at(i);
            const QColor crossColor = cross->pen().color();

            // 이 가로선과 동일한 색상을 가진 모든 줄기선들을 수집
            QList<LineItem *> matchedStemsForThisCross;
            for (int s = 0; s < stemCount; ++s) {
                LineItem *st = stemGroup.at(s);
                if (st->pen().color().rgb() == crossColor.rgb()) {
                    matchedStemsForThisCross.append(st);
                }
            }

            // 만약 동일한 색상의 줄기선이 전혀 없다면, 예외적으로 전체 줄기선을 대상으로 삼음
            if (matchedStemsForThisCross.isEmpty()) {
                matchedStemsForThisCross = stemGroup;
            }

            if (matchedStemsForThisCross.size() < 2) continue; // 대상 줄기선이 2개 미만이면 분할하지 않음

            // 이 교차선과의 교점 좌표(H/V 방향에 따라 X 또는 Y)를 기준으로 줄기선들을 정렬
            if (hIsCrossbar) {
                std::sort(matchedStemsForThisCross.begin(), matchedStemsForThisCross.end(), [this](LineItem *a, LineItem *b) {
                    return toLineF(a).p1().x() < toLineF(b).p1().x();
                });
            } else {
                std::sort(matchedStemsForThisCross.begin(), matchedStemsForThisCross.end(), [this](LineItem *a, LineItem *b) {
                    return toLineF(a).p1().y() < toLineF(b).p1().y();
                });
            }

            const QLineF qCross = toLineF(cross);
            
            // 이 가로선의 해당 색상 범위 내의 가장 양끝 줄기선들과의 교점을 기준으로 트림(분할) 구간을 산출
            QPointF P_L, P_R;
            if (getIntersection(qCross, toLineF(matchedStemsForThisCross.first()), P_L) &&
                getIntersection(qCross, toLineF(matchedStemsForThisCross.last()), P_R)) {
                
                const double lenSq = QPointF::dotProduct(qCross.p2() - qCross.p1(), qCross.p2() - qCross.p1());
                if (lenSq > 1e-9) {
                    const double tL = QPointF::dotProduct(P_L - qCross.p1(), qCross.p2() - qCross.p1()) / lenSq;
                    const double tR = QPointF::dotProduct(P_R - qCross.p1(), qCross.p2() - qCross.p1()) / lenSq;
                    
                    const double tMin = qMin(tL, tR);
                    const double tMax = qMax(tL, tR);
                    
                    constexpr double eps = 1e-2;
                    if (tMin >= -eps && tMax <= 1.0 + eps) {
                        const QPointF P1 = qCross.p1();
                        const QPointF P2 = qCross.p2();
                        
                        const double distL = QLineF(P1, P_L).length();
                        const double distR = QLineF(P1, P_R).length();
                        
                        QPointF ptNear1, ptNear2;
                        if (distL < distR) {
                            ptNear1 = P_L;
                            ptNear2 = P_R;
                        } else {
                            ptNear1 = P_R;
                            ptNear2 = P_L;
                        }
                        
                        // 방향 독립적인 안전한 분할:
                        // 원래 가로선을 P1 ~ ptNear1 로 축소 (p2를 ptNear1로 변경)
                        // 새 가로선 nl을 ptNear2 ~ P2 로 생성
                        LineItem *nl = new LineItem(MyVector(ptNear2), MyVector(P2));
                        nl->setPen(cross->pen());
                        nl->setLayerName(cross->layerName());
                        nl->setData(ML_DATA_GROUP_ID, cross->data(ML_DATA_GROUP_ID));
                        nl->setData(ML_DATA_ROLE,     cross->data(ML_DATA_ROLE));
                        ops << Op{ cross, P2, ptNear1, false, nl };
                    }
                }
            }
        }
    }
    // ── [C-코너 모드] ──────────────────────────────────────────────────────────
    else if (m_mode == CCorner) {
        // 모든 수평선 H에 대해 수직선 V0, V1과의 교점을 찾아서 그 사이를 분할
        if (vGroup.size() >= 2) {
            for (int i = 0; i < hGroup.size(); ++i) {
                LineItem *cross = hGroup.at(i);
                const QLineF qCross = toLineF(cross);
                QPointF P_L, P_R;
                if (getIntersection(qCross, toLineF(vGroup.at(0)), P_L) &&
                    getIntersection(qCross, toLineF(vGroup.at(1)), P_R)) {
                    
                    const double lenSq = QPointF::dotProduct(qCross.p2() - qCross.p1(), qCross.p2() - qCross.p1());
                    if (lenSq > 1e-9) {
                        const double tL = QPointF::dotProduct(P_L - qCross.p1(), qCross.p2() - qCross.p1()) / lenSq;
                        const double tR = QPointF::dotProduct(P_R - qCross.p1(), qCross.p2() - qCross.p1()) / lenSq;
                        const double tMin = qMin(tL, tR);
                        const double tMax = qMax(tL, tR);
                        const QPointF ptMin = (tL <= tR) ? P_L : P_R;
                        const QPointF ptMax = (tL <= tR) ? P_R : P_L;
                        
                        constexpr double eps = 1e-2;
                        if (tMin >= -eps && tMax <= 1.0 + eps) {
                            LineItem *nl = new LineItem(MyVector(ptMax), MyVector(qCross.p2()));
                            nl->setPen(cross->pen());
                            nl->setLayerName(cross->layerName());
                            nl->setData(ML_DATA_GROUP_ID, cross->data(ML_DATA_GROUP_ID));
                            nl->setData(ML_DATA_ROLE,     cross->data(ML_DATA_ROLE));
                            ops << Op{ cross, qCross.p2(), ptMin, false, nl };
                        }
                    }
                }
            }
        }
        // 모든 수직선 V에 대해 수평선 H0, H1과의 교점을 찾아서 그 사이를 분할
        if (hGroup.size() >= 2) {
            for (int i = 0; i < vGroup.size(); ++i) {
                LineItem *cross = vGroup.at(i);
                const QLineF qCross = toLineF(cross);
                QPointF P_L, P_R;
                if (getIntersection(qCross, toLineF(hGroup.at(0)), P_L) &&
                    getIntersection(qCross, toLineF(hGroup.at(1)), P_R)) {
                    
                    const double lenSq = QPointF::dotProduct(qCross.p2() - qCross.p1(), qCross.p2() - qCross.p1());
                    if (lenSq > 1e-9) {
                        const double tL = QPointF::dotProduct(P_L - qCross.p1(), qCross.p2() - qCross.p1()) / lenSq;
                        const double tR = QPointF::dotProduct(P_R - qCross.p1(), qCross.p2() - qCross.p1()) / lenSq;
                        const double tMin = qMin(tL, tR);
                        const double tMax = qMax(tL, tR);
                        const QPointF ptMin = (tL <= tR) ? P_L : P_R;
                        const QPointF ptMax = (tL <= tR) ? P_R : P_L;
                        
                        constexpr double eps = 1e-2;
                        if (tMin >= -eps && tMax <= 1.0 + eps) {
                            LineItem *nl = new LineItem(MyVector(ptMax), MyVector(qCross.p2()));
                            nl->setPen(cross->pen());
                            nl->setLayerName(cross->layerName());
                            nl->setData(ML_DATA_GROUP_ID, cross->data(ML_DATA_GROUP_ID));
                            nl->setData(ML_DATA_ROLE,     cross->data(ML_DATA_ROLE));
                            ops << Op{ cross, qCross.p2(), ptMin, false, nl };
                        }
                    }
                }
            }
        }
    }

    // 5. 실행 명령 등록
    IAppContext *ctx = getContext();
    if (ctx && ctx->undoStack() && !ops.isEmpty()) {
        ctx->undoStack()->beginMacro("Area Menu Corner Join");
        const int opCount = ops.size();
        for (int i = 0; i < opCount; ++i) {
            const Op &op = ops.at(i);
            ctx->undoStack()->push(new ExtendEntityCommand(op.line, op.oldPos, op.newPos, op.isFirstEnd));
            if (op.newLine) {
                ctx->undoStack()->push(new AddEntityCommand(m_scene, op.newLine));
            }
        }
        ctx->undoStack()->endMacro();

        if (ctx->commandDock()) {
            ctx->commandDock()->logMessage(tr("영역 조인 완료: %1개 선 정리됨.").arg(ops.size()), CommandDock::Info);
        }
    }
}

// ── AreaCornerJoinCommand ───────────────────────────────────────────────────
AreaCornerJoinCommand::AreaCornerJoinCommand(IAppContext *ctx, AreaCornerJoinTool::JoinMode mode, QObject *parent)
    : CommandBase(parent), m_ctx(ctx), m_mode(mode)
{}

void AreaCornerJoinCommand::execute()
{
    if (m_ctx) {
        m_ctx->startTool(new AreaCornerJoinTool(m_mode));
    }
}

